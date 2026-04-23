/* Freestanding <stddef.h> shim for BearSSL on AI_OS. */
#ifndef AI_OS_BEARSSL_SHIM_STDDEF_H
#define AI_OS_BEARSSL_SHIM_STDDEF_H

#include "../../include/types.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#endif
