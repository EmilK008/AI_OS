/* ===========================================================================
 * String utilities (freestanding - no libc)
 * =========================================================================== */
#include "string.h"

int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

int str_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

void str_copy(char *dst, const char *src) {
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
}

void str_ncopy(char *dst, const char *src, int max_len) {
    int i = 0;
    while (i < max_len - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++; b++;
    }
    return (int)(*(unsigned char *)a) - (int)(*(unsigned char *)b);
}

void *mem_set(void *dest, int val, uint32_t count) {
    uint8_t *d = (uint8_t *)dest;
    while (count--) *d++ = (uint8_t)val;
    return dest;
}

void *mem_copy(void *dest, const void *src, uint32_t count) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (count--) *d++ = *s++;
    return dest;
}

int str_to_int(const char *s) {
    int result = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return neg ? -result : result;
}
