#ifndef TYPES_H
#define TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

/* size_t / ptrdiff_t must be unsigned/signed to match the C standard
 * (BearSSL and other portable C code depend on this). On 32-bit x86
 * they are 32 bits wide. */
#ifndef __SIZE_T_DEFINED
#define __SIZE_T_DEFINED
typedef unsigned int       size_t;
#endif
#ifndef __PTRDIFF_T_DEFINED
#define __PTRDIFF_T_DEFINED
typedef signed int         ptrdiff_t;
#endif

#define NULL ((void*)0)
#ifndef __bool_true_false_are_defined
#define true  1
#define false 0
#define bool  _Bool
#define __bool_true_false_are_defined 1
#endif

#endif
