/* Freestanding <stdint.h> shim for BearSSL on AI_OS.
 * Re-uses the project's own integer typedefs so nothing conflicts. */
#ifndef AI_OS_BEARSSL_SHIM_STDINT_H
#define AI_OS_BEARSSL_SHIM_STDINT_H

#include "../../include/types.h"

/* Width macros BearSSL / inner.h sometimes uses. */
#define UINT8_MAX   0xFFu
#define UINT16_MAX  0xFFFFu
#define UINT32_MAX  0xFFFFFFFFu
#define UINT64_MAX  0xFFFFFFFFFFFFFFFFull

#define INT8_MAX    0x7F
#define INT16_MAX   0x7FFF
#define INT32_MAX   0x7FFFFFFF
#define INT64_MAX   0x7FFFFFFFFFFFFFFFll

#define INT8_MIN    (-INT8_MAX  - 1)
#define INT16_MIN   (-INT16_MAX - 1)
#define INT32_MIN   (-INT32_MAX - 1)
#define INT64_MIN   (-INT64_MAX - 1ll)

typedef uint32_t uintptr_t;
typedef int32_t  intptr_t;

/* Literal-suffix helpers */
#define UINT32_C(v) v ## u
#define UINT64_C(v) v ## ull
#define INT32_C(v)  v
#define INT64_C(v)  v ## ll

#endif
