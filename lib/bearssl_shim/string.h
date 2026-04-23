/* Freestanding <string.h> shim for BearSSL on AI_OS.
 * These are implemented in kernel/bearssl_shim.c. */
#ifndef AI_OS_BEARSSL_SHIM_STRING_H
#define AI_OS_BEARSSL_SHIM_STRING_H

#include "../../include/types.h"

void  *memcpy (void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset (void *dst, int c, size_t n);
int    memcmp (const void *a, const void *b, size_t n);
size_t strlen (const char *s);

#endif
