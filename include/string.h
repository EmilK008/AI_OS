#ifndef STRING_H
#define STRING_H

#include "types.h"

int   str_len(const char *s);
int   str_eq(const char *a, const char *b);
int   str_starts_with(const char *s, const char *prefix);
void  str_copy(char *dst, const char *src);
void  str_ncopy(char *dst, const char *src, int max_len);
int   str_cmp(const char *a, const char *b);
void *mem_set(void *dest, int val, uint32_t count);
void *mem_copy(void *dest, const void *src, uint32_t count);
int   str_to_int(const char *s);
void  mem_copy_fast(void *dest, const void *src, uint32_t count);

#endif
