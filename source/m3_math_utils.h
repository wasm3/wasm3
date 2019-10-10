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

static inline int __builtin_ctz(uint32_t x) {
    unsigned long ret;
    _BitScanForward(&ret, x);
    return (int)ret;
}

static inline int __builtin_ctzll(unsigned long long x) {
    unsigned long ret;
    _BitScanForward64(&ret, x);
    return (int)ret;
}

static inline int __builtin_clz(uint32_t x) {
    unsigned long ret;
    _BitScanReverse(&ret, x);
    return (int)(31 ^ ret);
}

static inline int __builtin_clzll(unsigned long long x) {
    unsigned long ret;
    _BitScanReverse64(&ret, x);
    return (int)(63 ^ ret);
}

#endif

/*
 * Rotr, Rotl
 */

static inline
u32 rotl32(u32 n, unsigned c) {
  const unsigned mask = (CHAR_BIT*sizeof(n)-1);
  c = c % 32;
  c &= mask;
  return (n<<c) | (n>>( (-c)&mask ));
}

static inline
u32 rotr32(u32 n, unsigned c) {
  const unsigned mask = (CHAR_BIT*sizeof(n)-1);
  c = c % 32;
  c &= mask;
  return (n>>c) | (n<<( (-c)&mask ));
}

static inline
u64 rotl64(u64 n, unsigned c) {
  const unsigned mask = (CHAR_BIT*sizeof(n)-1);
  c = c % 64;
  c &= mask;
  return (n<<c) | (n>>( (-c)&mask ));
}

static inline
u64 rotr64(u64 n, unsigned c) {
  const unsigned mask = (CHAR_BIT*sizeof(n)-1);
  c = c % 64;
  c &= mask;
  return (n>>c) | (n<<( (-c)&mask ));
}

/*
 * Integer Div, Rem
 */

#define OP_DIV_U(RES, A, B) \
	if (B == 0) return c_m3Err_trapDivisionByZero; \
	RES = A / B;

#define OP_REM_U(RES, A, B) \
	if (B == 0) return c_m3Err_trapDivisionByZero; \
	RES = A % B;

// 2's complement detection
#if (INT_MIN != -INT_MAX)

	#define OP_DIV_S(RES, A, B, TYPE_MIN) \
		if (B == 0) return c_m3Err_trapDivisionByZero; \
		if (B == -1 and A == TYPE_MIN) return c_m3Err_trapIntegerOverflow; \
		RES = A / B;

	#define OP_REM_S(RES, A, B, TYPE_MIN) \
		if (B == 0) return c_m3Err_trapDivisionByZero; \
		if (B == -1 and A == TYPE_MIN) RES = 0; \
		else RES = A % B;

#else

	#define OP_DIV_S(RES, A, B, TYPE_MIN) OP_DIV_U(RES, A, B)
	#define OP_REM_S(RES, A, B, TYPE_MIN) OP_REM_U(RES, A, B)

#endif

/*
 * Min, Max
 */
static inline
f32 min_f32(f32 a, f32 b) {
	if (isnan(a)) return a;
	if (isnan(b)) return b;
    f32 c = fminf(a, b);
    if (c==0 and a==b) { return signbit(a) ? a : b; }
    return c;
}

static inline
f32 max_f32(f32 a, f32 b) {
	if (isnan(a)) return a;
	if (isnan(b)) return b;
    f32 c = fmaxf(a, b);
    if (c==0 and a==b) { return signbit(a) ? b : a; }
    return c;
}

static inline
f64 min_f64(f64 a, f64 b) {
	if (isnan(a)) return a;
	if (isnan(b)) return b;
    f64 c = fmin(a, b);
    if (c==0 and a==b) { return signbit(a) ? a : b; }
    return c;
}

static inline
f64 max_f64(f64 a, f64 b) {
	if (isnan(a)) return a;
	if (isnan(b)) return b;
    f64 c = fmax(a, b);
    if (c==0 and a==b) { return signbit(a) ? b : a; }
    return c;
}

/*
 * Nearest
 */

static inline
f32 nearest_f32(f32 a) {
	if (a > 0.f and a <= 0.5f) return 0.f;
	if (a < 0.f and a >= -0.5f) return -0.f;
    return rintf(a);
}

static inline
f64 nearest_f64(f64 a) {
	if (a > 0.0 and a <= 0.5) return 0.0;
	if (a < 0.0 and a >= -0.5) return -0.0;
    return rint(a);
}


#endif /* m3_exception_h */
