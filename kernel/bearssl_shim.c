/* ===========================================================================
 * BearSSL libc symbol shim.
 *
 * BearSSL is written to freestanding-C89 but still expects a handful of
 * <string.h> symbols (memcpy, memset, memcmp, memmove, strlen) to be
 * provided by the toolchain's libc. AI_OS is built with -nostdlib, so we
 * provide them here. memcpy and memset forward to the existing
 * mem_copy / mem_set helpers in kernel/string.c.
 *
 * Note: gcc is also free to synthesise calls to memcpy/memset on its own
 * whenever it sees a large struct assignment or a zero-init, so these
 * symbols are useful beyond just BearSSL.
 * =========================================================================== */

#include "../include/types.h"
#include "../include/string.h"

void *memcpy(void *dst, const void *src, size_t n) {
    return mem_copy(dst, src, (uint32_t)n);
}

void *memset(void *dst, int c, size_t n) {
    return mem_set(dst, c, (uint32_t)n);
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}
