#ifndef NET_TLS_H
#define NET_TLS_H

#include "types.h"

/* TLS (HTTPS) client API, mirroring net_tcp_* / net_http_get.
 *
 * Single-connection: like net_tcp_*, only one TLS session is active at a
 * time. This is a hard limitation of the current networking layer.
 *
 * The underlying transport is net_tcp_*, so TLS inherits its MTU and
 * buffering semantics. */

/* Perform TCP connect + TLS handshake.
 *   ip           : remote IPv4 (host byte order)
 *   port         : remote port (normally 443)
 *   sni_hostname : hostname for SNI + certificate verification (must be
 *                  non-NULL; a trust anchor bundle is required for the
 *                  handshake to succeed).
 * Returns 0 on success, negative BearSSL error code on failure. */
int  net_tls_connect(uint32_t ip, uint16_t port, const char *sni_hostname);

/* Encrypt and send `len` bytes. Returns bytes sent or negative on error. */
int  net_tls_send(const void *data, uint16_t len);

/* Receive and decrypt at most `max` bytes into `buf`. Blocks up to
 * timeout_ticks. Returns bytes received, 0 on clean close, -1 on error. */
int  net_tls_receive(void *buf, uint16_t max, uint32_t timeout_ticks);

/* Send close_notify and tear down. */
void net_tls_close(void);

/* One-shot HTTPS GET, mirroring net_http_get. URL must start with
 * "https://". Returns number of bytes written to buf, or negative on
 * error. */
int  net_https_get(const char *url, char *buf, int max_size);

#endif
