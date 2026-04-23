/* ===========================================================================
 * BearSSL entropy / PRNG seeding for AI_OS.
 *
 * BearSSL normally pulls entropy from the host OS (CryptGenRandom,
 * /dev/urandom, getentropy). We have no host OS, so we gather entropy
 * ourselves from:
 *
 *   1. RDRAND if the CPU supports it (CPUID.01H:ECX.RDRAND[bit 30]).
 *   2. The Time Stamp Counter (RDTSC), sampled across scheduling delays.
 *   3. The PIT tick counter.
 *   4. The RTC (date/time + low bits of seconds).
 *   5. The NIC MAC address (constant but unique per machine).
 *
 * This is *not* cryptographic-grade entropy on its own if RDRAND is
 * missing, but on any QEMU or real hardware built in the last 15 years
 * RDRAND is universally available, and we mix the weaker sources in as
 * belt-and-suspenders.
 *
 * We also export `br_prng_seeder_system` (returning NULL) just to satisfy
 * BearSSL's internal fallback path in ssl_engine.c; the actual seeding
 * happens via br_ssl_engine_inject_entropy() before every handshake,
 * driven by ai_os_collect_entropy() below.
 * =========================================================================== */

#include "../include/types.h"
#include "../include/timer.h"
#include "../include/rtc.h"
#include "../include/net.h"

/* Forward-declare BearSSL's seeder typedef without pulling in its headers
 * (they depend on the shim and we want this file to build with the
 * normal kernel CFLAGS). The linker only cares about the symbol name. */
typedef int (*br_prng_seeder_stub)(void *);
br_prng_seeder_stub br_prng_seeder_system(const char **name);

br_prng_seeder_stub
br_prng_seeder_system(const char **name)
{
    if (name) *name = (const char *)0;
    return (br_prng_seeder_stub)0;
}

/* ---------------------------------------------------------------------- */

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void cpuid(uint32_t leaf,
                         uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ __volatile__("cpuid"
                         : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                         : "a"(leaf));
}

static bool rdrand_available(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    return (c & (1u << 30)) != 0;
}

static bool rdrand32(uint32_t *out) {
    /* Retry loop per Intel's recommendation. */
    uint8_t ok = 0;
    for (int i = 0; i < 10; i++) {
        __asm__ __volatile__(
            "rdrand %0; setc %1"
            : "=r"(*out), "=qm"(ok)
        );
        if (ok) return true;
    }
    return false;
}

/* Collect `len` bytes of best-effort entropy into buf.
 * Returns the number of bytes written (always == len). */
size_t ai_os_collect_entropy(uint8_t *buf, size_t len) {
    size_t i = 0;

    bool have_rdrand = rdrand_available();

    /* Start from a mixed seed of non-RDRAND sources. */
    uint64_t mix = rdtsc();
    mix ^= (uint64_t)timer_get_ticks() * 0x9E3779B97F4A7C15ull;

    struct rtc_time t;
    rtc_read(&t);
    mix ^= ((uint64_t)t.year     << 40) |
           ((uint64_t)t.month    << 32) |
           ((uint64_t)t.day      << 24) |
           ((uint64_t)t.hours    << 16) |
           ((uint64_t)t.minutes  <<  8) |
           ((uint64_t)t.seconds  <<  0);

    struct net_config *nc = net_get_config();
    if (nc) {
        uint64_t macmix = 0;
        for (int j = 0; j < 6; j++) macmix = (macmix << 8) | nc->mac[j];
        mix ^= macmix * 0xBF58476D1CE4E5B9ull;
    }

    while (i < len) {
        uint32_t w;
        if (have_rdrand && rdrand32(&w)) {
            mix ^= ((uint64_t)w << 32);
        }
        /* Always re-sample TSC - fine-grained timing jitter. */
        mix ^= rdtsc();
        /* SplitMix64 to diffuse the bits. */
        mix += 0x9E3779B97F4A7C15ull;
        uint64_t z = mix;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z =  z ^ (z >> 31);

        for (int j = 0; j < 8 && i < len; j++) {
            buf[i++] = (uint8_t)(z >> (j * 8));
        }
    }
    return i;
}
