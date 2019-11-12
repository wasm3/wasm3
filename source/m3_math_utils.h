//
//  m3_math_utils.h
//  m3
//
//  Created by Volodymyr Shymanksyy on 8/10/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#ifndef m3_math_utils_h
#define m3_math_utils_h

#include "m3_core.h"

#include <math.h>
#include <limits.h>

#if defined(M3_COMPILER_MSVC)

#include <intrin.h>

#define __builtin_popcount    __popcnt
#define __builtin_popcountll  __popcnt64

static inline
int __builtin_ctz(uint32_t x) {
    unsigned long ret;
    _BitScanForward(&ret, x);
    return (int)ret;
}

static inline
int __builtin_ctzll(unsigned long long x) {
    unsigned long ret;
    _BitScanForward64(&ret, x);
    return (int)ret;
}

static inline
int __builtin_clz(uint32_t x) {
    unsigned long ret;
    _BitScanReverse(&ret, x);
    return (int)(31 ^ ret);
}

static inline
int __builtin_clzll(unsigned long long x) {
    unsigned long ret;
    _BitScanReverse64(&ret, x);
    return (int)(63 ^ ret);
}

#endif

// TODO: not sure why, signbit is actually defined in math.h
#if defined(ESP8266)
    #define signbit(__x) \
            ((sizeof(__x) == sizeof(float))  ?  __signbitf(__x) : __signbitd(__x))
#endif

/*
 * Rotr, Rotl
 */

static inline
u32 rotl32(u32 n, unsigned c) {
    const unsigned mask = CHAR_BIT * sizeof(n) - 1;
    c &= mask & 31;
    return (n << c) | (n >> ((-c) & mask));
}

static inline
u32 rotr32(u32 n, unsigned c) {
    const unsigned mask = CHAR_BIT * sizeof(n) - 1;
    c &= mask & 31;
    return (n >> c) | (n << ((-c) & mask));
}

static inline
u64 rotl64(u64 n, unsigned c) {
    const unsigned mask = CHAR_BIT * sizeof(n) - 1;
    c &= mask & 63;
    return (n << c) | (n >> ((-c) & mask));
}

static inline
u64 rotr64(u64 n, unsigned c) {
    const unsigned mask = CHAR_BIT * sizeof(n) - 1;
    c &= mask & 63;
    return (n >> c) | (n << ((-c) & mask));
}

/*
 * Integer Div, Rem
 */

#define OP_DIV_U(RES, A, B)                                  \
    if (UNLIKELY(B == 0)) return c_m3Err_trapDivisionByZero; \
    RES = A / B;

#define OP_REM_U(RES, A, B)                                  \
    if (UNLIKELY(B == 0)) return c_m3Err_trapDivisionByZero; \
    RES = A % B;

// 2's complement detection
#if (INT_MIN != -INT_MAX)

    #define OP_DIV_S(RES, A, B, TYPE_MIN)                        \
        if (UNLIKELY(B == 0)) return c_m3Err_trapDivisionByZero; \
        if (UNLIKELY(B == -1 and A == TYPE_MIN)) {               \
            return c_m3Err_trapIntegerOverflow;                  \
        }                                                        \
        RES = A / B;

    #define OP_REM_S(RES, A, B, TYPE_MIN)                        \
        if (UNLIKELY(B == 0)) return c_m3Err_trapDivisionByZero; \
        if (UNLIKELY(B == -1 and A == TYPE_MIN)) RES = 0;        \
        else RES = A % B;

#else

    #define OP_DIV_S(RES, A, B, TYPE_MIN) OP_DIV_U(RES, A, B)
    #define OP_REM_S(RES, A, B, TYPE_MIN) OP_REM_U(RES, A, B)

#endif

/*
 * Trunc
 */

#define OP_TRUNC_I32(RES, A)                                \
    if (UNLIKELY(isnan(A))) {                               \
        return c_m3Err_trapIntegerConversion;               \
    }                                                       \
    if (UNLIKELY(A < INT32_MIN or A >= INT32_MAX)) {        \
        return c_m3Err_trapIntegerOverflow;                 \
    }                                                       \
    RES = A;

#define OP_TRUNC_U32(RES, A)                                \
    if (UNLIKELY(isnan(A))) {                               \
        return c_m3Err_trapIntegerConversion;               \
    }                                                       \
    if (UNLIKELY(A <= -1 or A >= UINT32_MAX)) {             \
        return c_m3Err_trapIntegerOverflow;                 \
    }                                                       \
    RES = A;

#define OP_TRUNC_I64(RES, A)                                \
    if (UNLIKELY(isnan(A))) {                               \
        return c_m3Err_trapIntegerConversion;               \
    }                                                       \
    if (UNLIKELY(A < INT64_MIN or A >= INT64_MAX)) {        \
        return c_m3Err_trapIntegerOverflow;                 \
    }                                                       \
    RES = A;

#define OP_TRUNC_U64(RES, A)                                \
    if (UNLIKELY(isnan(A))) {                               \
        return c_m3Err_trapIntegerConversion;               \
    }                                                       \
    if (UNLIKELY(A <= -1 or A >= UINT64_MAX)) {             \
        return c_m3Err_trapIntegerOverflow;                 \
    }                                                       \
    RES = A;

/*
 * Min, Max
 */
static inline
f32 min_f32(f32 a, f32 b) {
    if (UNLIKELY(isnan(a) or isnan(b))) return NAN;
    if (UNLIKELY(a == 0 and a == b)) return signbit(a) ? a : b;
    return a > b ? b : a;
}

static inline
f32 max_f32(f32 a, f32 b) {
    if (UNLIKELY(isnan(a) or isnan(b))) return NAN;
    if (UNLIKELY(a == 0 and a == b)) return signbit(a) ? b : a;
    return a > b ? a : b;
}

static inline
f64 min_f64(f64 a, f64 b) {
    if (UNLIKELY(isnan(a) or isnan(b))) return NAN;
    if (UNLIKELY(a == 0 and a == b)) return signbit(a) ? a : b;
    return a > b ? b : a;
}

static inline
f64 max_f64(f64 a, f64 b) {
    if (UNLIKELY(isnan(a) or isnan(b))) return NAN;
    if (UNLIKELY(a == 0 and a == b)) return signbit(a) ? b : a;
    return a > b ? a : b;
}

#endif /* m3_math_utils_h */
