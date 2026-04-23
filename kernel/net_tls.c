/* ===========================================================================
 * TLS / HTTPS client for AI_OS, powered by BearSSL.
 *
 * Architecture:
 *   Browser / app  ---- net_https_get / net_tls_* ----\
 *                                                      \
 *   net_tls.c      ---- br_ssl_client_context       ----\
 *                       (BearSSL state machine)          |
 *                                                        |
 *   net.c          ---- net_tcp_connect / send / recv  -/
 *                       (our own single-connection TCP)
 *                                                        |
 *   rtl8139.c      ---- NIC driver                    --/
 *
 * BearSSL's SSL engine is a pure state machine: we feed it raw bytes
 * from TCP, it hands us back raw bytes to send over TCP, and on the
 * "app" side we inject plaintext to send and drain plaintext received.
 * This file just pumps those four queues.
 * =========================================================================== */

#include "types.h"
#include "net.h"
#include "net_tls.h"
#include "timer.h"
#include "rtc.h"

/* This translation unit is built with BEARSSL_CFLAGS, whose first -I is
 * lib/bearssl_shim. That shim's <string.h> declares memcpy/memset/memcmp/
 * strlen (provided by kernel/bearssl_shim.c), which is what we actually
 * need here. We do NOT include the project's "string.h" to avoid a name
 * clash on that header. */
#include <string.h>

/* BearSSL public headers. These live under lib/bearssl/inc/ and must be
 * reached via the BearSSL-specific include path set by build.sh when
 * compiling this file (BEARSSL_CFLAGS adds -Ilib/bearssl/inc). */
#include "bearssl.h"

/* ------------------------------------------------------------------------ */
/* Trust anchors (defined in kernel/trust_anchors.c). Empty for now; all
 * handshakes will fail BR_ERR_X509_NOT_TRUSTED until populated. */
extern const br_x509_trust_anchor ai_os_trust_anchors[];
extern const unsigned int         ai_os_trust_anchors_num;

/* Entropy source provided by kernel/bearssl_entropy.c. */
size_t ai_os_collect_entropy(uint8_t *buf, size_t len);

/* Write to QEMU debug port 0xE9 so TLS diagnostics land in the same file
 * as kernel.c's dbg_print output (run QEMU with -debugcon file:...). */
static void net_dbg(const char *s) {
    while (*s) {
        __asm__ __volatile__("outb %0, %1"
                             : : "a"((unsigned char)*s++), "Nd"((unsigned short)0xE9));
    }
}

static void net_dbg_hex(uint32_t v) {
    char buf[11]; buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        unsigned n = (v >> ((7 - i) * 4)) & 0xF;
        buf[2 + i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
    }
    buf[10] = 0;
    net_dbg(buf);
}

static void net_dbg_dec(uint32_t v) {
    char buf[11]; int i = 10; buf[i] = 0;
    if (v == 0) { buf[--i] = '0'; }
    else { while (v && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; } }
    net_dbg(&buf[i]);
}

/* net.c stores IPs in host byte order with the *first* octet in the low
 * byte (bytes_to_ip: b[0] | b[1]<<8 | b[2]<<16 | b[3]<<24). Print that
 * LSB-first so we don't confuse anyone. */
static void net_dbg_ip(uint32_t ip) {
    net_dbg_dec( ip        & 0xFF); net_dbg(".");
    net_dbg_dec((ip >>  8) & 0xFF); net_dbg(".");
    net_dbg_dec((ip >> 16) & 0xFF); net_dbg(".");
    net_dbg_dec((ip >> 24) & 0xFF);
}

/* ------------------------------------------------------------------------ */
/* Static engine state. All buffers in .bss so we don't blow the stack. */

static br_ssl_client_context    g_sc;
static br_x509_minimal_context  g_xc;
/* BearSSL wants one big I/O buffer; we use the larger "mono" size which
 * handles one record at a time (half-duplex). That's ~17 KB. */
static unsigned char            g_iobuf[BR_SSL_BUFSIZE_MONO];
static bool                     g_tls_up = false;

/* ------------------------------------------------------------------------ */
/* Unix time from RTC, for X.509 validity checking. */

static bool is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}
static const int MDAYS[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

static uint32_t rtc_to_unix_days(const struct rtc_time *t) {
    int year = (int)t->century * 100 + (int)t->year;
    /* Days since 1970-01-01. */
    uint32_t days = 0;
    for (int y = 1970; y < year; y++) days += is_leap(y) ? 366 : 365;
    for (int m = 0; m < (int)t->month - 1; m++) {
        days += MDAYS[m];
        if (m == 1 && is_leap(year)) days += 1;
    }
    days += (uint32_t)t->day - 1;
    return days;
}

/* Populate BearSSL's time for X.509: days since year 0, and seconds in day.
 * BearSSL's reference epoch is year 0 AD; Unix epoch 1970-01-01 is day
 * 719528 in that count. */
static void x509_set_time_from_rtc(void) {
    struct rtc_time t;
    rtc_read(&t);
    uint32_t unix_days = rtc_to_unix_days(&t);
    uint32_t days_since_year0 = unix_days + 719528u;
    uint32_t sec = (uint32_t)t.hours * 3600u
                 + (uint32_t)t.minutes * 60u
                 + (uint32_t)t.seconds;
    br_x509_minimal_set_time(&g_xc, days_since_year0, sec);
}

/* ------------------------------------------------------------------------ */
/* Engine pump.
 *
 * One iteration moves bytes in whichever direction the engine currently
 * wants. Returns:
 *    > 0  : progress made (caller should re-check state)
 *      0  : nothing to do right now, but engine isn't broken
 *    < 0  : engine is closed or errored (caller should abort)
 *
 * `want_send_app` is non-zero while the caller is trying to push app
 * data; this tells the pump not to block on SENDREC if SENDAPP is
 * available.
 */
static int tls_pump_once(uint32_t timeout_ticks) {
    br_ssl_engine_context *eng = &g_sc.eng;
    unsigned state = br_ssl_engine_current_state(eng);

    if (state & BR_SSL_CLOSED) {
        int err = br_ssl_engine_last_error(eng);
        return err == 0 ? -1 : -err;
    }

    /* Drain records we need to transmit first (back-pressure on the
     * remote).
     * NOTE: net_tcp_send() returns 0 on success and -1 on error; it
     * does *not* return a byte count. So we ack the whole chunk. */
    if (state & BR_SSL_SENDREC) {
        size_t len;
        unsigned char *buf = br_ssl_engine_sendrec_buf(eng, &len);
        uint16_t chunk = len > 1400 ? 1400 : (uint16_t)len;
        if (net_tcp_send(buf, chunk) < 0) return -1;
        br_ssl_engine_sendrec_ack(eng, (size_t)chunk);
        return 1;
    }

    /* Feed received record bytes in.
     * net_tcp_receive() returns: >0 bytes received, 0 on clean close,
     * -1 on timeout (no bytes yet). A timeout is not fatal - the outer
     * driver loop retries until it hits its own wall-clock timeout. */
    if (state & BR_SSL_RECVREC) {
        size_t len;
        unsigned char *buf = br_ssl_engine_recvrec_buf(eng, &len);
        uint16_t chunk = len > 1400 ? 1400 : (uint16_t)len;
        int n = net_tcp_receive(buf, chunk, timeout_ticks);
        if (n <= 0) return 0;  /* timeout or clean close: try again */
        br_ssl_engine_recvrec_ack(eng, (size_t)n);
        return 1;
    }

    return 0;
}

/* Drive the engine until a condition is true or it errors out. */
static int tls_drive_until(unsigned wanted_state_mask, uint32_t timeout_ticks) {
    uint32_t start = timer_get_ticks();
    for (;;) {
        unsigned state = br_ssl_engine_current_state(&g_sc.eng);
        if (state & BR_SSL_CLOSED) {
            int err = br_ssl_engine_last_error(&g_sc.eng);
            return err == 0 ? -1 : -err;
        }
        if (state & wanted_state_mask) return 0;

        int r = tls_pump_once(10);
        if (r < 0) return r;

        if ((timer_get_ticks() - start) > timeout_ticks) return -1;
    }
}

/* ------------------------------------------------------------------------ */
/* Public API */

int net_tls_connect(uint32_t ip, uint16_t port, const char *sni_hostname) {
    if (g_tls_up) {
        net_dbg("TLS: already up\n");
        return -1;
    }
    if (!sni_hostname || !sni_hostname[0]) {
        net_dbg("TLS: hostname required\n");
        return -1;
    }

    /* Underlying TCP first. */
    if (net_tcp_connect(ip, port) < 0) {
        net_dbg("TLS: TCP connect failed\n");
        return -2;
    }

    /* Initialise BearSSL client with full profile (all standard cipher
     * suites, X.509 minimal validator). */
    br_ssl_client_init_full(&g_sc, &g_xc,
                            ai_os_trust_anchors,
                            (size_t)ai_os_trust_anchors_num);

    br_ssl_engine_set_buffer(&g_sc.eng, g_iobuf, sizeof g_iobuf, 0);

    /* Seed the engine's PRNG from our collected entropy. 32 bytes is
     * plenty for a DRBG seed. */
    unsigned char seed[32];
    ai_os_collect_entropy(seed, sizeof seed);
    br_ssl_engine_inject_entropy(&g_sc.eng, seed, sizeof seed);

    /* Provide current time for certificate validity checks. Without this,
     * BearSSL rejects all certs as "not yet valid" or "expired". */
    x509_set_time_from_rtc();

    if (!br_ssl_client_reset(&g_sc, sni_hostname, 0)) {
        net_dbg("TLS: client_reset failed\n");
        net_tcp_close();
        return -3;
    }

    /* Drive the handshake to completion: wait for SENDAPP|RECVAPP.
     * Timer is 100 Hz, so 3000 ticks = 30 s. Handshakes against real
     * internet hosts typically need ~1-2 s but can spike higher on
     * lossy links or when the remote does a big cert chain. */
    net_dbg("TLS: handshake start\n");
    int r = tls_drive_until(BR_SSL_SENDAPP | BR_SSL_RECVAPP, 3000);
    if (r < 0) {
        int be = br_ssl_engine_last_error(&g_sc.eng);
        net_dbg("TLS: handshake failed, drive_rc=");
        net_dbg_dec((uint32_t)(int32_t)(r < 0 ? -r : r));
        net_dbg(" bearssl_err=");
        net_dbg_dec((uint32_t)be);
        net_dbg("\n");
        net_tcp_close();
        return r;
    }

    net_dbg("TLS: handshake ok\n");
    g_tls_up = true;
    return 0;
}

int net_tls_send(const void *data, uint16_t len) {
    if (!g_tls_up) return -1;

    const unsigned char *p = (const unsigned char *)data;
    uint16_t remaining = len;
    while (remaining > 0) {
        int r = tls_drive_until(BR_SSL_SENDAPP, 1000);  /* 10 s per chunk */
        if (r < 0) return r;

        size_t avail;
        unsigned char *buf = br_ssl_engine_sendapp_buf(&g_sc.eng, &avail);
        if (avail == 0) {
            /* Engine wants us to flush; pump and retry. */
            br_ssl_engine_flush(&g_sc.eng, 0);
            continue;
        }
        size_t chunk = avail < remaining ? avail : remaining;
        memcpy(buf, p, chunk);
        br_ssl_engine_sendapp_ack(&g_sc.eng, chunk);
        br_ssl_engine_flush(&g_sc.eng, 0);
        p += chunk;
        remaining -= (uint16_t)chunk;
    }
    return (int)len;
}

int net_tls_receive(void *buf, uint16_t max, uint32_t timeout_ticks) {
    if (!g_tls_up) return -1;

    int r = tls_drive_until(BR_SSL_RECVAPP, timeout_ticks);
    if (r < 0) {
        /* Clean close looks like CLOSED with err == 0, mapped to -1 by
         * tls_drive_until; we surface as 0 here. */
        unsigned state = br_ssl_engine_current_state(&g_sc.eng);
        if ((state & BR_SSL_CLOSED) &&
            br_ssl_engine_last_error(&g_sc.eng) == 0) {
            return 0;
        }
        return r;
    }

    size_t avail;
    unsigned char *src = br_ssl_engine_recvapp_buf(&g_sc.eng, &avail);
    size_t take = avail < max ? avail : max;
    memcpy(buf, src, take);
    br_ssl_engine_recvapp_ack(&g_sc.eng, take);
    return (int)take;
}

void net_tls_close(void) {
    if (!g_tls_up) return;
    br_ssl_engine_close(&g_sc.eng);
    /* Best-effort flush of close_notify. */
    for (int i = 0; i < 16; i++) {
        int r = tls_pump_once(10);
        if (r <= 0) break;
    }
    net_tcp_close();
    g_tls_up = false;
}

/* ------------------------------------------------------------------------ */
/* HTTPS GET (mirrors net_http_get in net.c) */

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

/* Tiny URL splitter; lifted in spirit from parse_url() in net.c but
 * specialised for https. */
static int parse_https_url(const char *url, char *host, int host_max,
                           uint16_t *port, char *path, int path_max) {
    *port = 443;
    if (!starts_with(url, "https://")) return -1;
    url += 8;

    int i = 0;
    while (*url && *url != ':' && *url != '/' && i < host_max - 1) {
        host[i++] = *url++;
    }
    host[i] = '\0';
    if (i == 0) return -1;

    if (*url == ':') {
        url++;
        *port = 0;
        while (*url >= '0' && *url <= '9') {
            *port = *port * 10 + (*url - '0');
            url++;
        }
    }

    int j = 0;
    if (*url == '\0') {
        if (path_max >= 2) { path[0] = '/'; path[1] = '\0'; }
    } else {
        while (*url && j < path_max - 1) path[j++] = *url++;
        path[j] = '\0';
    }
    return 0;
}

int net_https_get(const char *url, char *buf, int max_size) {
    char     host[128];
    char     path[256];
    uint16_t port;

    net_dbg("HTTPS: url="); net_dbg(url); net_dbg("\n");

    if (parse_https_url(url, host, sizeof host, &port, path, sizeof path) < 0) {
        net_dbg("HTTPS: bad URL\n");
        return -1;
    }
    net_dbg("HTTPS: host="); net_dbg(host);
    net_dbg(" port="); net_dbg_dec(port);
    net_dbg(" path="); net_dbg(path); net_dbg("\n");

    uint32_t ip;
    if (!net_dns_resolve(host, &ip)) {
        net_dbg("HTTPS: DNS fail for "); net_dbg(host); net_dbg("\n");
        return -2;
    }
    net_dbg("HTTPS: dns ok ip="); net_dbg_ip(ip); net_dbg("\n");
    net_dbg("HTTPS: trust_anchors="); net_dbg_dec(ai_os_trust_anchors_num); net_dbg("\n");

    int r = net_tls_connect(ip, port, host);
    if (r < 0) {
        net_dbg("HTTPS: TLS connect fail, rc="); net_dbg_dec((uint32_t)(int32_t)(r < 0 ? -r : r)); net_dbg("\n");
        return -3;
    }
    net_dbg("HTTPS: tls up, sending request\n");

    /* Build and send the HTTP request. */
    char req[512];
    int  n = 0;
    const char *hdr[] = {
        "GET ", path, " HTTP/1.0\r\nHost: ", host,
        "\r\nUser-Agent: AI_OS/0.3\r\nConnection: close\r\n\r\n", 0
    };
    for (int i = 0; hdr[i]; i++) {
        const char *s = hdr[i];
        while (*s && n < (int)sizeof(req) - 1) req[n++] = *s++;
    }
    if (net_tls_send(req, (uint16_t)n) < 0) {
        net_dbg("HTTPS: send failed\n");
        net_tls_close();
        return -4;
    }
    net_dbg("HTTPS: request sent, draining response\n");

    /* Drain response until close_notify / TCP close or buffer full. */
    int total = 0;
    while (total < max_size - 1) {
        int got = net_tls_receive(buf + total, (uint16_t)(max_size - 1 - total), 300);
        if (got <= 0) break;
        total += got;
    }
    buf[total] = '\0';

    net_dbg("HTTPS: total bytes="); net_dbg_dec((uint32_t)total); net_dbg("\n");
    net_tls_close();
    return total;
}
