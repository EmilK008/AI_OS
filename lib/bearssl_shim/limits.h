/* Freestanding <limits.h> shim for BearSSL on AI_OS (32-bit x86). */
#ifndef AI_OS_BEARSSL_SHIM_LIMITS_H
#define AI_OS_BEARSSL_SHIM_LIMITS_H

#define CHAR_BIT    8
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255
#define CHAR_MIN    SCHAR_MIN
#define CHAR_MAX    SCHAR_MAX

#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535

#define INT_MIN     (-2147483647 - 1)
#define INT_MAX     2147483647
#define UINT_MAX    0xFFFFFFFFu

#define LONG_MIN    INT_MIN
#define LONG_MAX    INT_MAX
#define ULONG_MAX   UINT_MAX

#define LLONG_MIN   (-9223372036854775807ll - 1)
#define LLONG_MAX   9223372036854775807ll
#define ULLONG_MAX  0xFFFFFFFFFFFFFFFFull

#endif
