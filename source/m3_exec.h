//
//  m3_exec.h
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.


#ifndef m3_exec_h
#define m3_exec_h

// TODO: all these functions could move over to the .c at some point. normally, i'd say screw it,
// but it might prove useful to be able to compile m3_exec alone w/ optimizations while the remaining
// code is at debug O0


// About the naming convention of these operations/macros (_rs, _sr_, _ss, _srs, etc.)
//------------------------------------------------------------------------------------------------------
//   - 'r' means register and 's' means slot
//   - the first letter is the top of the stack
//
//  so, for example, _rs means the first operand (the first thing pushed to the stack) is in a slot
//  and the second operand (the top of the stack) is in a register
//------------------------------------------------------------------------------------------------------


#include "m3_math_utils.h"
#include "m3_compile.h"
#include "m3_env.h"
#include "m3_exec_defs.h"

#include <limits.h>

d_m3BeginExternC

# define rewrite_op(OP)             * ((void **) (_pc-1)) = (void*)(OP)

# define immediate(TYPE)            * ((TYPE *) _pc++)
# define skip_immediate(TYPE)       (_pc++)

# define slot(TYPE)                 * (TYPE *) (_sp + immediate (i32))
# define slot_ptr(TYPE)             (TYPE *) (_sp + immediate (i32))


# if d_m3EnableOpProfiling
                                    d_m3RetSig  profileOp   (d_m3OpSig, cstr_t i_operationName);
#   define nextOp()                 return profileOp (d_m3OpAllArgs, __FUNCTION__)
# elif d_m3EnableOpTracing
                                    d_m3RetSig  debugOp     (d_m3OpSig, cstr_t i_operationName);
#   define nextOp()                 return debugOp (d_m3OpAllArgs, __FUNCTION__)
# else
#   define nextOp()                 nextOpDirect()
# endif

#define jumpOp(PC)                  jumpOpDirect(PC)

d_m3RetSig  Call  (d_m3OpSig)
{
    m3ret_t possible_trap = m3_Yield ();
    if (UNLIKELY(possible_trap)) return possible_trap;

    nextOpDirect();
}

// TODO: OK, this needs some explanation here ;0

#define d_m3CommutativeOpMacro(RES, REG, TYPE, NAME, OP, ...) \
d_m3Op(TYPE##_##NAME##_rs)                              \
{                                                       \
    TYPE operand = slot (TYPE);                         \
    OP((RES), operand, ((TYPE) REG), ##__VA_ARGS__);    \
    nextOp ();                                          \
}                                                       \
d_m3Op(TYPE##_##NAME##_ss)                              \
{                                                       \
    TYPE operand2 = slot (TYPE);                        \
    TYPE operand1 = slot (TYPE);                        \
    OP((RES), operand1, operand2, ##__VA_ARGS__);       \
    nextOp ();                                          \
}

#define d_m3OpMacro(RES, REG, TYPE, NAME, OP, ...)      \
d_m3Op(TYPE##_##NAME##_sr)                              \
{                                                       \
    TYPE operand = slot (TYPE);                         \
    OP((RES), ((TYPE) REG), operand, ##__VA_ARGS__);    \
    nextOp ();                                          \
}                                                       \
d_m3CommutativeOpMacro(RES, REG, TYPE,NAME, OP, ##__VA_ARGS__)

// Accept macros
#define d_m3CommutativeOpMacro_i(TYPE, NAME, MACRO, ...)    d_m3CommutativeOpMacro  ( _r0,  _r0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3OpMacro_i(TYPE, NAME, MACRO, ...)               d_m3OpMacro             ( _r0,  _r0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3CommutativeOpMacro_f(TYPE, NAME, MACRO, ...)    d_m3CommutativeOpMacro  (_fp0, _fp0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3OpMacro_f(TYPE, NAME, MACRO, ...)               d_m3OpMacro             (_fp0, _fp0, TYPE, NAME, MACRO, ##__VA_ARGS__)

#define M3_FUNC(RES, A, B, OP)  (RES) = OP((A), (B))    // Accept functions: res = OP(a,b)
#define M3_OPER(RES, A, B, OP)  (RES) = ((A) OP (B))    // Accept operators: res = a OP b

#define d_m3CommutativeOpFunc_i(TYPE, NAME, OP)     d_m3CommutativeOpMacro_i    (TYPE, NAME, M3_FUNC, OP)
#define d_m3OpFunc_i(TYPE, NAME, OP)                d_m3OpMacro_i               (TYPE, NAME, M3_FUNC, OP)
#define d_m3CommutativeOpFunc_f(TYPE, NAME, OP)     d_m3CommutativeOpMacro_f    (TYPE, NAME, M3_FUNC, OP)
#define d_m3OpFunc_f(TYPE, NAME, OP)                d_m3OpMacro_f               (TYPE, NAME, M3_FUNC, OP)

#define d_m3CommutativeOp_i(TYPE, NAME, OP)         d_m3CommutativeOpMacro_i    (TYPE, NAME, M3_OPER, OP)
#define d_m3Op_i(TYPE, NAME, OP)                    d_m3OpMacro_i               (TYPE, NAME, M3_OPER, OP)
#define d_m3CommutativeOp_f(TYPE, NAME, OP)         d_m3CommutativeOpMacro_f    (TYPE, NAME, M3_OPER, OP)
#define d_m3Op_f(TYPE, NAME, OP)                    d_m3OpMacro_f               (TYPE, NAME, M3_OPER, OP)

// compare needs to be distinct for fp 'cause the result must be _r0
#define d_m3CompareOp_f(TYPE, NAME, OP)             d_m3OpMacro                 (_r0, _fp0, TYPE, NAME, M3_OPER, OP)
#define d_m3CommutativeCmpOp_f(TYPE, NAME, OP)      d_m3CommutativeOpMacro      (_r0, _fp0, TYPE, NAME, M3_OPER, OP)


//-----------------------

// signed
d_m3CommutativeOp_i (i32, Equal,            ==)     d_m3CommutativeOp_i (i64, Equal,            ==)
d_m3CommutativeOp_i (i32, NotEqual,         !=)     d_m3CommutativeOp_i (i64, NotEqual,         !=)

d_m3Op_i (i32, LessThan,                    < )     d_m3Op_i (i64, LessThan,                    < )
d_m3Op_i (i32, GreaterThan,                 > )     d_m3Op_i (i64, GreaterThan,                 > )
d_m3Op_i (i32, LessThanOrEqual,             <=)     d_m3Op_i (i64, LessThanOrEqual,             <=)
d_m3Op_i (i32, GreaterThanOrEqual,          >=)     d_m3Op_i (i64, GreaterThanOrEqual,          >=)

// unsigned
d_m3Op_i (u32, LessThan,                    < )     d_m3Op_i (u64, LessThan,                    < )
d_m3Op_i (u32, GreaterThan,                 > )     d_m3Op_i (u64, GreaterThan,                 > )
d_m3Op_i (u32, LessThanOrEqual,             <=)     d_m3Op_i (u64, LessThanOrEqual,             <=)
d_m3Op_i (u32, GreaterThanOrEqual,          >=)     d_m3Op_i (u64, GreaterThanOrEqual,          >=)

#if d_m3HasFloat
d_m3CommutativeCmpOp_f (f32, Equal,         ==)     d_m3CommutativeCmpOp_f (f64, Equal,         ==)
d_m3CommutativeCmpOp_f (f32, NotEqual,      !=)     d_m3CommutativeCmpOp_f (f64, NotEqual,      !=)
d_m3CompareOp_f (f32, LessThan,             < )     d_m3CompareOp_f (f64, LessThan,             < )
d_m3CompareOp_f (f32, GreaterThan,          > )     d_m3CompareOp_f (f64, GreaterThan,          > )
d_m3CompareOp_f (f32, LessThanOrEqual,      <=)     d_m3CompareOp_f (f64, LessThanOrEqual,      <=)
d_m3CompareOp_f (f32, GreaterThanOrEqual,   >=)     d_m3CompareOp_f (f64, GreaterThanOrEqual,   >=)
#endif

d_m3CommutativeOp_i (i32, Add,              +)      d_m3CommutativeOp_i (i64, Add,              +)
d_m3CommutativeOp_i (i32, Multiply,         *)      d_m3CommutativeOp_i (i64, Multiply,         *)

d_m3Op_i (i32, Subtract,                    -)      d_m3Op_i (i64, Subtract,                    -)

#define OP_SHL_32(X,N) ((X) << ((u32)(N) % 32))
#define OP_SHL_64(X,N) ((X) << ((u64)(N) % 64))
#define OP_SHR_32(X,N) ((X) >> ((u32)(N) % 32))
#define OP_SHR_64(X,N) ((X) >> ((u64)(N) % 64))

d_m3OpFunc_i (u32, ShiftLeft,       OP_SHL_32)      d_m3OpFunc_i (u64, ShiftLeft,       OP_SHL_64)
d_m3OpFunc_i (i32, ShiftRight,      OP_SHR_32)      d_m3OpFunc_i (i64, ShiftRight,      OP_SHR_64)
d_m3OpFunc_i (u32, ShiftRight,      OP_SHR_32)      d_m3OpFunc_i (u64, ShiftRight,      OP_SHR_64)

d_m3CommutativeOp_i (u32, And,              &)
d_m3CommutativeOp_i (u32, Or,               |)
d_m3CommutativeOp_i (u32, Xor,              ^)

d_m3CommutativeOp_i (u64, And,              &)
d_m3CommutativeOp_i (u64, Or,               |)
d_m3CommutativeOp_i (u64, Xor,              ^)

#if d_m3HasFloat
d_m3CommutativeOp_f (f32, Add,              +)      d_m3CommutativeOp_f (f64, Add,              +)
d_m3CommutativeOp_f (f32, Multiply,         *)      d_m3CommutativeOp_f (f64, Multiply,         *)
d_m3Op_f (f32, Subtract,                    -)      d_m3Op_f (f64, Subtract,                    -)
d_m3Op_f (f32, Divide,                      /)      d_m3Op_f (f64, Divide,                      /)
#endif

d_m3OpFunc_i(u32, Rotl, rotl32)
d_m3OpFunc_i(u32, Rotr, rotr32)
d_m3OpFunc_i(u64, Rotl, rotl64)
d_m3OpFunc_i(u64, Rotr, rotr64)

d_m3OpMacro_i(u32, Divide, OP_DIV_U);
d_m3OpMacro_i(i32, Divide, OP_DIV_S, INT32_MIN);
d_m3OpMacro_i(u64, Divide, OP_DIV_U);
d_m3OpMacro_i(i64, Divide, OP_DIV_S, INT64_MIN);

d_m3OpMacro_i(u32, Remainder, OP_REM_U);
d_m3OpMacro_i(i32, Remainder, OP_REM_S, INT32_MIN);
d_m3OpMacro_i(u64, Remainder, OP_REM_U);
d_m3OpMacro_i(i64, Remainder, OP_REM_S, INT64_MIN);

#if d_m3HasFloat
d_m3OpFunc_f(f32, Min, min_f32);
d_m3OpFunc_f(f32, Max, max_f32);
d_m3OpFunc_f(f64, Min, min_f64);
d_m3OpFunc_f(f64, Max, max_f64);

d_m3OpFunc_f(f32, CopySign, copysignf);
d_m3OpFunc_f(f64, CopySign, copysign);
#endif

// Unary operations
// Note: This macro follows the principle of d_m3OpMacro

#define d_m3UnaryMacro(RES, REG, TYPE, NAME, OP, ...)   \
d_m3Op(TYPE##_##NAME##_r)                           \
{                                                   \
    OP((RES), (TYPE) REG, ##__VA_ARGS__);           \
    nextOp ();                                      \
}                                                   \
d_m3Op(TYPE##_##NAME##_s)                           \
{                                                   \
    TYPE operand = slot (TYPE);                     \
    OP((RES), operand, ##__VA_ARGS__);              \
    nextOp ();                                      \
}

#define M3_UNARY(RES, X, OP) (RES) = OP(X)
#define d_m3UnaryOp_i(TYPE, NAME, OPERATION)        d_m3UnaryMacro( _r0,  _r0, TYPE, NAME, M3_UNARY, OPERATION)
#define d_m3UnaryOp_f(TYPE, NAME, OPERATION)        d_m3UnaryMacro(_fp0, _fp0, TYPE, NAME, M3_UNARY, OPERATION)

#if d_m3HasFloat
d_m3UnaryOp_f (f32, Abs,        fabsf);         d_m3UnaryOp_f (f64, Abs,        fabs);
d_m3UnaryOp_f (f32, Ceil,       ceilf);         d_m3UnaryOp_f (f64, Ceil,       ceil);
d_m3UnaryOp_f (f32, Floor,      floorf);        d_m3UnaryOp_f (f64, Floor,      floor);
d_m3UnaryOp_f (f32, Trunc,      truncf);        d_m3UnaryOp_f (f64, Trunc,      trunc);
d_m3UnaryOp_f (f32, Sqrt,       sqrtf);         d_m3UnaryOp_f (f64, Sqrt,       sqrt);
d_m3UnaryOp_f (f32, Nearest,    rintf);         d_m3UnaryOp_f (f64, Nearest,    rint);
d_m3UnaryOp_f (f32, Negate,     -);             d_m3UnaryOp_f (f64, Negate,     -);
#endif

#define OP_EQZ(x) ((x) == 0)

d_m3UnaryOp_i (i32, EqualToZero, OP_EQZ)
d_m3UnaryOp_i (i64, EqualToZero, OP_EQZ)

// clz(0), ctz(0) results are undefined for rest platforms, fix it
#if (defined(__i386__) || defined(__x86_64__)) && !(defined(__AVX2__) || (defined(__ABM__) && defined(__BMI__)))
    #define OP_CLZ_32(x) (UNLIKELY((x) == 0) ? 32 : __builtin_clz(x))
    #define OP_CTZ_32(x) (UNLIKELY((x) == 0) ? 32 : __builtin_ctz(x))
    // for 64-bit instructions branchless approach more preferable
    #define OP_CLZ_64(x) (__builtin_clzll((x) | (1LL <<  0)) + OP_EQZ(x))
    #define OP_CTZ_64(x) (__builtin_ctzll((x) | (1LL << 63)) + OP_EQZ(x))
#elif defined(__ppc__) || defined(__ppc64__)
// PowerPC is defined for __builtin_clz(0) and __builtin_ctz(0).
// See (https://github.com/aquynh/capstone/blob/master/MathExtras.h#L99)
    #define OP_CLZ_32(x) __builtin_clz(x)
    #define OP_CTZ_32(x) __builtin_ctz(x)
    #define OP_CLZ_64(x) __builtin_clzll(x)
    #define OP_CTZ_64(x) __builtin_ctzll(x)
#else
    #define OP_CLZ_32(x) (UNLIKELY((x) == 0) ? 32 : __builtin_clz(x))
    #define OP_CTZ_32(x) (UNLIKELY((x) == 0) ? 32 : __builtin_ctz(x))
    #define OP_CLZ_64(x) (UNLIKELY((x) == 0) ? 64 : __builtin_clzll(x))
    #define OP_CTZ_64(x) (UNLIKELY((x) == 0) ? 64 : __builtin_ctzll(x))
#endif

d_m3UnaryOp_i (u32, Clz, OP_CLZ_32)
d_m3UnaryOp_i (u64, Clz, OP_CLZ_64)

d_m3UnaryOp_i (u32, Ctz, OP_CTZ_32)
d_m3UnaryOp_i (u64, Ctz, OP_CTZ_64)

d_m3UnaryOp_i (u32, Popcnt, __builtin_popcount)
d_m3UnaryOp_i (u64, Popcnt, __builtin_popcountll)

#define OP_WRAP_I64(X) ((X) & 0x00000000ffffffff)

d_m3Op(i32_Wrap_i64_r)
{
    _r0 = OP_WRAP_I64((i64) _r0);
    nextOp ();
}

d_m3Op(i32_Wrap_i64_s)
{
    i64 operand = slot (i64);
    _r0 = OP_WRAP_I64(operand);
    nextOp ();
}

// Integer sign extension operations
#define OP_EXTEND8_S_I32(X)  ((int32_t)(int8_t)(X))
#define OP_EXTEND16_S_I32(X) ((int32_t)(int16_t)(X))
#define OP_EXTEND8_S_I64(X)  ((int64_t)(int8_t)(X))
#define OP_EXTEND16_S_I64(X) ((int64_t)(int16_t)(X))
#define OP_EXTEND32_S_I64(X) ((int64_t)(int32_t)(X))

d_m3UnaryOp_i (i32, Extend8_s,  OP_EXTEND8_S_I32)
d_m3UnaryOp_i (i32, Extend16_s, OP_EXTEND16_S_I32)
d_m3UnaryOp_i (i64, Extend8_s,  OP_EXTEND8_S_I64)
d_m3UnaryOp_i (i64, Extend16_s, OP_EXTEND16_S_I64)
d_m3UnaryOp_i (i64, Extend32_s, OP_EXTEND32_S_I64)

#define d_m3TruncMacro(DEST, SRC, TYPE, NAME, FROM, OP, ...)   \
d_m3Op(TYPE##_##NAME##_##FROM##_r_r)                \
{                                                   \
    OP((DEST), (FROM) SRC, ##__VA_ARGS__);          \
    nextOp ();                                      \
}                                                   \
d_m3Op(TYPE##_##NAME##_##FROM##_r_s)                \
{                                                   \
    FROM * stack = slot_ptr (FROM);                 \
    OP((DEST), (* stack), ##__VA_ARGS__);           \
    nextOp ();                                      \
}                                                   \
d_m3Op(TYPE##_##NAME##_##FROM##_s_r)                \
{                                                   \
    TYPE * dest = slot_ptr (TYPE);                  \
    OP((* dest), (FROM) SRC, ##__VA_ARGS__);        \
    nextOp ();                                      \
}                                                   \
d_m3Op(TYPE##_##NAME##_##FROM##_s_s)                \
{                                                   \
    FROM * stack = slot_ptr (FROM);                 \
    TYPE * dest = slot_ptr (TYPE);                  \
    OP((* dest), (* stack), ##__VA_ARGS__);         \
    nextOp ();                                      \
}

#if d_m3HasFloat
d_m3TruncMacro(_r0, _fp0, i32, Trunc, f32, OP_I32_TRUNC_F32)
d_m3TruncMacro(_r0, _fp0, u32, Trunc, f32, OP_U32_TRUNC_F32)
d_m3TruncMacro(_r0, _fp0, i32, Trunc, f64, OP_I32_TRUNC_F64)
d_m3TruncMacro(_r0, _fp0, u32, Trunc, f64, OP_U32_TRUNC_F64)

d_m3TruncMacro(_r0, _fp0, i64, Trunc, f32, OP_I64_TRUNC_F32)
d_m3TruncMacro(_r0, _fp0, u64, Trunc, f32, OP_U64_TRUNC_F32)
d_m3TruncMacro(_r0, _fp0, i64, Trunc, f64, OP_I64_TRUNC_F64)
d_m3TruncMacro(_r0, _fp0, u64, Trunc, f64, OP_U64_TRUNC_F64)

d_m3TruncMacro(_r0, _fp0, i32, TruncSat, f32, OP_I32_TRUNC_SAT_F32)
d_m3TruncMacro(_r0, _fp0, u32, TruncSat, f32, OP_U32_TRUNC_SAT_F32)
d_m3TruncMacro(_r0, _fp0, i32, TruncSat, f64, OP_I32_TRUNC_SAT_F64)
d_m3TruncMacro(_r0, _fp0, u32, TruncSat, f64, OP_U32_TRUNC_SAT_F64)

d_m3TruncMacro(_r0, _fp0, i64, TruncSat, f32, OP_I64_TRUNC_SAT_F32)
d_m3TruncMacro(_r0, _fp0, u64, TruncSat, f32, OP_U64_TRUNC_SAT_F32)
d_m3TruncMacro(_r0, _fp0, i64, TruncSat, f64, OP_I64_TRUNC_SAT_F64)
d_m3TruncMacro(_r0, _fp0, u64, TruncSat, f64, OP_U64_TRUNC_SAT_F64)
#endif

#define d_m3TypeModifyOp(REG_TO, REG_FROM, TO, NAME, FROM)  \
d_m3Op(TO##_##NAME##_##FROM##_r)                            \
{                                                           \
    REG_TO = (TO) ((FROM) REG_FROM);                        \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_##NAME##_##FROM##_s)                            \
{                                                           \
    FROM from = slot (FROM);                                \
    REG_TO = (TO) (from);                                   \
    nextOp ();                                              \
}

// Int to int
d_m3TypeModifyOp (_r0, _r0, i64, Extend, i32);
d_m3TypeModifyOp (_r0, _r0, i64, Extend, u32);

// Float to float
#if d_m3HasFloat
d_m3TypeModifyOp (_fp0, _fp0, f32, Demote, f64);
d_m3TypeModifyOp (_fp0, _fp0, f64, Promote, f32);
#endif

#define d_m3TypeConvertOp(REG_TO, REG_FROM, TO, NAME, FROM) \
d_m3Op(TO##_##NAME##_##FROM##_r_r)                          \
{                                                           \
    REG_TO = (TO) ((FROM) REG_FROM);                        \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_##NAME##_##FROM##_s_r)                          \
{                                                           \
    slot (TO) = (TO) ((FROM) REG_FROM);                     \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_##NAME##_##FROM##_r_s)                          \
{                                                           \
    FROM from = slot (FROM);                                \
    REG_TO = (TO) (from);                                   \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_##NAME##_##FROM##_s_s)                          \
{                                                           \
    FROM from = slot (FROM);                                \
    slot (TO) = (TO) (from);                                \
    nextOp ();                                              \
}

// Int to float
#if d_m3HasFloat
d_m3TypeConvertOp (_fp0, _r0, f64, Convert, i32);
d_m3TypeConvertOp (_fp0, _r0, f64, Convert, u32);
d_m3TypeConvertOp (_fp0, _r0, f64, Convert, i64);
d_m3TypeConvertOp (_fp0, _r0, f64, Convert, u64);

d_m3TypeConvertOp (_fp0, _r0, f32, Convert, i32);
d_m3TypeConvertOp (_fp0, _r0, f32, Convert, u32);
d_m3TypeConvertOp (_fp0, _r0, f32, Convert, i64);
d_m3TypeConvertOp (_fp0, _r0, f32, Convert, u64);
#endif

#define d_m3ReinterpretOp(REG, TO, SRC, FROM)               \
d_m3Op(TO##_Reinterpret_##FROM##_r_r)                       \
{                                                           \
    union { FROM c; TO t; } u;                              \
    u.c = (FROM) SRC;                                       \
    REG = u.t;                                              \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_Reinterpret_##FROM##_r_s)                       \
{                                                           \
    union { FROM c; TO t; } u;                              \
    u.c = slot (FROM);                                      \
    REG = u.t;                                              \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_Reinterpret_##FROM##_s_r)                       \
{                                                           \
    union { FROM c; TO t; } u;                              \
    u.c = (FROM) SRC;                                       \
    slot (TO) = u.t;                                        \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_Reinterpret_##FROM##_s_s)                       \
{                                                           \
    union { FROM c; TO t; } u;                              \
    u.c = slot (FROM);                                      \
    slot (TO) = u.t;                                        \
    nextOp ();                                              \
}

#if d_m3HasFloat
d_m3ReinterpretOp (_r0, i32, _fp0, f32)
d_m3ReinterpretOp (_r0, i64, _fp0, f64)
d_m3ReinterpretOp (_fp0, f32, _r0, i32)
d_m3ReinterpretOp (_fp0, f64, _r0, i64)
#endif


d_m3Op  (GetGlobal_s32)
{
    u32 * global = immediate (u32 *);
    slot (u32) = * global;                        //  printf ("get global: %p %" PRIi64 "\n", global, *global);

    nextOp ();
}


d_m3Op  (GetGlobal_s64)
{
    u64 * global = immediate (u64 *);
    slot (u64) = * global;                        // printf ("get global: %p %" PRIi64 "\n", global, *global);

    nextOp ();
}


d_m3Op  (SetGlobal_i32)
{
    u32 * global = immediate (u32 *);
    * global = (u32) _r0;                         //  printf ("set global: %p %" PRIi64 "\n", global, _r0);

    nextOp ();
}


d_m3Op  (SetGlobal_i64)
{
    u64 * global = immediate (u64 *);
    * global = (u64) _r0;                         //  printf ("set global: %p %" PRIi64 "\n", global, _r0);

    nextOp ();
}


d_m3Op  (Call)
{
    pc_t callPC                 = immediate (pc_t);
    i32 stackOffset             = immediate (i32);
    IM3Memory memory            = m3MemInfo (_mem);

    m3stack_t sp = _sp + stackOffset;

    m3ret_t r = Call (callPC, sp, _mem, d_m3OpDefaultArgs);

    if (r == 0)
    {
        _mem = memory->mallocated;
        nextOp ();
    }
    else trapOp (r);
}


d_m3Op  (CallIndirect)
{
    u32 tableIndex              = slot (u32);
    IM3Module module            = immediate (IM3Module);
    IM3FuncType type            = immediate (IM3FuncType);
    i32 stackOffset             = immediate (i32);
    IM3Memory memory            = m3MemInfo (_mem);

    m3stack_t sp = _sp + stackOffset;

    m3ret_t r = m3Err_none;

    if (tableIndex < module->table0Size)
    {
        IM3Function function = module->table0 [tableIndex];

        if (function)
        {
            if (type == function->funcType)
            {
                if (UNLIKELY(not function->compiled))
                    r = Compile_Function (function);

                if (not r)
                {
                    r = Call (function->compiled, sp, _mem, d_m3OpDefaultArgs);

                    if (not r)
                    {
                        _mem = memory->mallocated;
                        nextOpDirect ();
                    }
                }
            }
            else r = m3Err_trapIndirectCallTypeMismatch;
        }
        else r = m3Err_trapTableElementIsNull;
    }
    else r = m3Err_trapTableIndexOutOfRange;

    trapOp (r);
}


d_m3Op  (CallRawFunction)
{
    M3ImportContext ctx;

    M3RawCall call = (M3RawCall) (* _pc++);
    ctx.function = immediate (IM3Function);
    ctx.userdata = immediate (void *);
    u64* const sp = ((u64*)_sp);

#if d_m3EnableStrace
    IM3FuncType ftype = ctx.function->funcType;

    FILE* out = stderr;
    char outbuff[1024];
    char* outp = outbuff;
    char* oute = outbuff+1024;

    outp += snprintf(outp, oute-outp, "%s.%s(", ctx.function->import.moduleUtf8, ctx.function->import.fieldUtf8);

    const int nArgs = ftype->numArgs;
    const int nRets = ftype->numRets;
    for (int i=0; i<nArgs; i++) {
        const int type = ftype->types[nRets + i];
        switch (type) {
        case c_m3Type_i32:  outp += snprintf(outp, oute-outp, "%" PRIi32, *(i32*)(sp+i)); break;
        case c_m3Type_i64:  outp += snprintf(outp, oute-outp, "%" PRIi64, *(i64*)(sp+i)); break;
        case c_m3Type_f32:  outp += snprintf(outp, oute-outp, "%" PRIf32, *(f32*)(sp+i)); break;
        case c_m3Type_f64:  outp += snprintf(outp, oute-outp, "%" PRIf64, *(f64*)(sp+i)); break;
        default:            outp += snprintf(outp, oute-outp, "<unknown type %d>", type); break;
        }
        outp += snprintf(outp, oute-outp, (i < nArgs-1) ? ", " : ")");
    }
#endif

    m3ret_t possible_trap = call (m3MemRuntime(_mem), &ctx, sp, m3MemData(_mem));

#if d_m3EnableStrace
    if (possible_trap) {
        fprintf(out, "%s -> %s\n", outbuff, possible_trap);
    } else {
        switch (GetSingleRetType(ftype)) {
        case c_m3Type_none: fprintf(out, "%s\n", outbuff); break;
        case c_m3Type_i32:  fprintf(out, "%s = %" PRIi32 "\n", outbuff, *(i32*)sp); break;
        case c_m3Type_i64:  fprintf(out, "%s = %" PRIi64 "\n", outbuff, *(i64*)sp); break;
        case c_m3Type_f32:  fprintf(out, "%s = %" PRIf32 "\n", outbuff, *(f32*)sp); break;
        case c_m3Type_f64:  fprintf(out, "%s = %" PRIf64 "\n", outbuff, *(f64*)sp); break;
        }
    }
#endif

    trapOp (possible_trap);
}


d_m3Op  (MemCurrent)
{
    IM3Memory memory            = m3MemInfo (_mem);

    _r0 = memory->numPages;

    nextOp ();
}


d_m3Op  (MemGrow)
{
    IM3Runtime runtime          = m3MemRuntime(_mem);
    IM3Memory memory            = & runtime->memory;

    u32 numPagesToGrow = (u32) _r0;
    _r0 = memory->numPages;

    if (numPagesToGrow)
    {
        u32 requiredPages = memory->numPages + numPagesToGrow;

        M3Result r = ResizeMemory (runtime, requiredPages);
        if (r)
            _r0 = -1;

        _mem = memory->mallocated;
    }

    nextOp ();
}


void  ReportError2  (IM3Function i_function, m3ret_t i_result);


// it's a debate: should the compilation be trigger be the caller or callee page.
// it's a much easier to put it in the caller pager. if it's in the callee, either the entire page
// has be left dangling or it's just a stub that jumps to a newly acquire page.  In Gestalt, I opted
// for the stub approach. Stubbing makes it easier to dynamically free the compilation. You can also
// do both.
d_m3Op  (Compile)
{
    rewrite_op (op_Call);

    IM3Function function        = immediate (IM3Function);

    m3ret_t result = m3Err_none;

    if (UNLIKELY(not function->compiled)) // check to see if function was compiled since this operation was emitted.
        result = Compile_Function (function);

    if (not result)
    {
        // patch up compiled pc and call rewriten op_Call
        * ((void**) --_pc) = (void*) (function->compiled);
        --_pc;
        nextOpDirect ();
    }
    else ReportError2 (function, result);

    trapOp (result);
}



d_m3Op  (Entry)
{
    d_m3ClearRegisters

    IM3Function function = immediate (IM3Function);

#if d_m3SkipStackCheck
    if (true)
#else
    if ((void *) ((m3slot_t *) _sp + function->maxStackSlots) < _mem->maxStack)
#endif
    {
                                                                m3log (exec, " enter %p > %s %s", _pc - 2, GetFunctionName(function), SPrintFunctionArgList (function, _sp));

#if defined(DEBUG)
        function->hits++;
#endif
        u8 * stack = (u8 *) ((m3slot_t *) _sp + function->numArgSlots);

        memset (stack, 0x0, function->numLocalBytes);
        stack += function->numLocalBytes;

        if (function->constants)
        {
            memcpy (stack, function->constants, function->numConstantBytes);
        }

        m3ret_t r = nextOpImpl ();

#       if d_m3LogExec
            char str [100] = { '!', 0 };

            if (not r)
                SPrintArg (str, 99, _sp, GetSingleRetType(function->funcType));

            m3log (exec, " exit  < %s %s %s   %s", GetFunctionName(function), function->funcType->numRets ? "->" : "", str, r ? (cstr_t)r : "");
#       elif d_m3LogStackTrace
            if (r)
                printf (" ** %s  %p\n", GetFunctionName(function), _sp);
#       endif

        return r;
    }
    else trapOp (m3Err_trapStackOverflow);
}


d_m3Op  (Loop)
{
    // regs are unused coming into a loop anyway
    // this reduces code size & stack usage
    d_m3ClearRegisters

    m3ret_t r;

    IM3Memory memory = m3MemInfo (_mem);

    do
    {
        r = nextOpImpl ();                     // printf ("loop: %p\n", r);
        // linear memory pointer needs refreshed here because the block it's looping over
        // can potentially invoke the grow operation.
        _mem = memory->mallocated;
    }
    while (r == _pc);

    trapOp (r);
}


d_m3Op  (Branch)
{
    jumpOp (* _pc);
}


d_m3Op  (If_r)
{
    i32 condition = (i32) _r0;

    pc_t elsePC = immediate (pc_t);

    if (condition)
        nextOp ();
    else
        jumpOp (elsePC);
}


d_m3Op  (If_s)
{
    i32 condition = slot (i32);

    pc_t elsePC = immediate (pc_t);

    if (condition)
        nextOp ();
    else
        jumpOp (elsePC);
}


d_m3Op  (BranchTable)
{
    i32 branchIndex = slot (i32);           // branch index is always in a slot
    i32 numTargets  = immediate (i32);

    pc_t * branches = (pc_t *) _pc;

    if (branchIndex < 0 or branchIndex > numTargets)
        branchIndex = numTargets; // the default index

    jumpOp (branches [branchIndex]);
}


#define d_m3SetRegisterSetSlot(TYPE, REG) \
d_m3Op  (SetRegister_##TYPE)            \
{                                       \
    REG = slot (TYPE);                  \
    nextOp ();                          \
}                                       \
                                        \
d_m3Op (SetSlot_##TYPE)                 \
{                                       \
    slot (TYPE) = (TYPE) REG;           \
    nextOp ();                          \
}                                       \
                                        \
d_m3Op (PreserveSetSlot_##TYPE)         \
{                                       \
    TYPE * stack     = slot_ptr (TYPE); \
    TYPE * preserve  = slot_ptr (TYPE); \
                                        \
    * preserve = * stack;               \
    * stack = (TYPE) REG;               \
                                        \
    nextOp ();                          \
}

d_m3SetRegisterSetSlot (i32, _r0)
d_m3SetRegisterSetSlot (i64, _r0)
#if d_m3HasFloat
d_m3SetRegisterSetSlot (f32, _fp0)
d_m3SetRegisterSetSlot (f64, _fp0)
#endif

d_m3Op (CopySlot_32)
{
    u32 * dst = slot_ptr (u32);
    u32 * src = slot_ptr (u32);

    * dst = * src;

    nextOp ();
}


d_m3Op (PreserveCopySlot_32)
{
    u32 * dest      = slot_ptr (u32);
    u32 * src       = slot_ptr (u32);
    u32 * preserve  = slot_ptr (u32);

    * preserve = * dest;
    * dest = * src;

    nextOp ();
}


d_m3Op (CopySlot_64)
{
    u64 * dst = slot_ptr (u64);
    u64 * src = slot_ptr (u64);

    * dst = * src;                  // printf ("copy: %p <- %" PRIi64 " <- %p\n", dst, * dst, src);

    nextOp ();
}


d_m3Op (PreserveCopySlot_64)
{
    u64 * dest      = slot_ptr (u64);
    u64 * src       = slot_ptr (u64);
    u64 * preserve  = slot_ptr (u64);

    * preserve = * dest;
    * dest = * src;

    nextOp ();
}


#if d_m3EnableOpTracing
//--------------------------------------------------------------------------------------------------------
d_m3Op  (DumpStack)
{
    u32 opcodeIndex         = immediate (u32);
    u32 stackHeight         = immediate (u32);
    IM3Function function    = immediate (IM3Function);

    cstr_t funcName = (function) ? GetFunctionName(function) : "";

    printf (" %4d ", opcodeIndex);
    printf (" %-25s     r0: 0x%016" PRIx64 "  i:%" PRIi64 "  u:%" PRIu64 "\n", funcName, _r0, _r0, _r0);
#if d_m3HasFloat
    printf ("                                    fp0: %" PRIf64 "\n", _fp0);
#endif
    m3stack_t sp = _sp;

    for (u32 i = 0; i < stackHeight; ++i)
    {
        cstr_t kind = "";

        printf ("%p  %5s  %2d: 0x%" PRIx64 "  i:%" PRIi64 "\n", sp, kind, i, (u64) *(sp), (i64) *(sp));

        ++sp;
    }
    printf ("---------------------------------------------------------------------------------------------------------\n");

    nextOpDirect();
}
#endif


#define d_m3Select_i(TYPE, REG)                 \
d_m3Op  (Select_##TYPE##_rss)                   \
{                                               \
    i32 condition = (i32) _r0;                  \
                                                \
    TYPE operand2 = slot (TYPE);                \
    TYPE operand1 = slot (TYPE);                \
                                                \
    REG = (condition) ? operand1 : operand2;    \
                                                \
    nextOp ();                                  \
}                                               \
                                                \
d_m3Op  (Select_##TYPE##_srs)                   \
{                                               \
    i32 condition = slot (i32);                 \
                                                \
    TYPE operand2 = (TYPE) REG;                 \
    TYPE operand1 = slot (TYPE);                \
                                                \
    REG = (condition) ? operand1 : operand2;    \
                                                \
    nextOp ();                                  \
}                                               \
                                                \
d_m3Op  (Select_##TYPE##_ssr)                   \
{                                               \
    i32 condition = slot (i32);                 \
                                                \
    TYPE operand2 = slot (TYPE);                \
    TYPE operand1 = (TYPE) REG;                 \
                                                \
    REG = (condition) ? operand1 : operand2;    \
                                                \
    nextOp ();                                  \
}                                               \
                                                \
d_m3Op  (Select_##TYPE##_sss)                   \
{                                               \
    i32 condition = slot (i32);                 \
                                                \
    TYPE operand2 = slot (TYPE);                \
    TYPE operand1 = slot (TYPE);                \
                                                \
    REG = (condition) ? operand1 : operand2;    \
                                                \
    nextOp ();                                  \
}


d_m3Select_i (i32, _r0)
d_m3Select_i (i64, _r0)


#define d_m3Select_f(TYPE, REG, LABEL, SELECTOR)  \
d_m3Op  (Select_##TYPE##_##LABEL##ss)           \
{                                               \
    i32 condition = (i32) SELECTOR;             \
                                                \
    TYPE operand2 = slot (TYPE);                \
    TYPE operand1 = slot (TYPE);                \
                                                \
    REG = (condition) ? operand1 : operand2;    \
                                                \
    nextOp ();                                  \
}                                               \
                                                \
d_m3Op  (Select_##TYPE##_##LABEL##rs)           \
{                                               \
    i32 condition = (i32) SELECTOR;             \
                                                \
    TYPE operand2 = (TYPE) REG;                 \
    TYPE operand1 = slot (TYPE);                \
                                                \
    REG = (condition) ? operand1 : operand2;    \
                                                \
    nextOp ();                                  \
}                                               \
                                                \
d_m3Op  (Select_##TYPE##_##LABEL##sr)           \
{                                               \
    i32 condition = (i32) SELECTOR;             \
                                                \
    TYPE operand2 = slot (TYPE);                \
    TYPE operand1 = (TYPE) REG;                 \
                                                \
    REG = (condition) ? operand1 : operand2;    \
                                                \
    nextOp ();                                  \
}

#if d_m3HasFloat
d_m3Select_f (f32, _fp0, r, _r0)
d_m3Select_f (f32, _fp0, s, slot (i32))

d_m3Select_f (f64, _fp0, r, _r0)
d_m3Select_f (f64, _fp0, s, slot (i32))
#endif

d_m3Op  (Return)
{
    m3StackCheck();
    trapOp (m3Err_none);
}


d_m3Op  (BranchIf_r)
{
    i32 condition   = (i32) _r0;
    pc_t branch     = immediate (pc_t);

    if (condition)
    {
        jumpOp (branch);
    }
    else nextOp ();
}


d_m3Op  (BranchIf_s)
{
    i32 condition   = slot (i32);
    pc_t branch     = immediate (pc_t);

    if (condition)
    {
        jumpOp (branch);
    }
    else nextOp ();
}


// branching to blocks that produce a (int) value
#define d_m3BranchIf(TYPE, LABEL, COND)         \
d_m3Op  (TYPE##_BranchIf_##LABEL##s)            \
{                                               \
    i32 condition   = (i32) COND;               \
    TYPE value      = slot (TYPE);              \
    pc_t branch     = immediate (pc_t);         \
                                                \
    if (condition)                              \
    {                                           \
        _r0 = value;                            \
        jumpOp (branch);                        \
    }                                           \
    else nextOp ();                             \
}


d_m3BranchIf (i32, r, _r0)
d_m3BranchIf (i64, r, _r0)
d_m3BranchIf (i32, s, slot (i32))
d_m3BranchIf (i64, s, slot (i32))



d_m3Op  (ContinueLoop)
{
    // TODO: this is where execution can "escape" the M3 code and callback to the client / fiber switch
    // OR it can go in the Loop operation. I think it's best to do here. adding code to the loop operation
    // has the potential to increase its native-stack usage. (don't forget ContinueLoopIf too.)

    void * loopId = immediate (void *);
    trapOp (loopId);
}


d_m3Op  (ContinueLoopIf)
{
    i32 condition = (i32) _r0;
    void * loopId = immediate (void *);

    if (condition)
    {
        trapOp (loopId);
    }
    else nextOp ();
}


d_m3Op  (Const32)
{
    u32 value = * (u32 *)_pc++;
    slot (u32) = value;
    nextOp ();
}


d_m3Op  (Const64)
{
    u64 value = * (u64 *)_pc;
    _pc += (M3_SIZEOF_PTR == 4) ? 2 : 1;
    slot (u64) = value;
    nextOp ();
}

d_m3Op  (Unsupported)
{                                                   m3log (exec, "*** unsupported ***");
    trapOp ("unsupported instruction executed");
}

d_m3Op  (Unreachable)
{                                                   m3log (exec, "*** trapping ***");
    m3StackCheck();
    trapOp (m3Err_trapUnreachable);
}


d_m3Op  (End)
{
    m3StackCheck();
    trapOp (m3Err_none);
}


d_m3Op  (SetGlobal_s32)
{
    u32 * global = immediate (u32 *);
    * global = slot (u32);

    nextOp ();
}


d_m3Op  (SetGlobal_s64)
{
    u64 * global = immediate (u64 *);
    * global = slot (u64);

    nextOp ();
}

#if d_m3HasFloat
d_m3Op  (SetGlobal_f32)
{
    f32 * global = immediate (f32 *);
    * global = _fp0;

    nextOp ();
}


d_m3Op  (SetGlobal_f64)
{
    f64 * global = immediate (f64 *);
    * global = _fp0;

    nextOp ();
}
#endif


#if d_m3SkipMemoryBoundsCheck
#  define m3MemCheck(x) true
#else
#  define m3MemCheck(x) LIKELY(x)
#endif

#ifdef DEBUG
  #define d_outOfBounds trapOp (ErrorRuntime (m3Err_trapOutOfBoundsMemoryAccess, \
                        _mem->runtime, "memory size: %zu; access offset: %zu",      \
                        _mem->length, operand))
#else
  #define d_outOfBounds trapOp (m3Err_trapOutOfBoundsMemoryAccess)
#endif

// memcpy here is to support non-aligned access on some platforms.
// TODO: check if this is optimized-out on x86/x64, and performance impact

#define d_m3Load(REG,DEST_TYPE,SRC_TYPE)                \
d_m3Op(DEST_TYPE##_Load_##SRC_TYPE##_r)                 \
{                                                       \
    u32 offset = immediate (u32);                       \
    u64 operand = (u32) _r0;                            \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (SRC_TYPE) <= _mem->length     \
    )) {                                                \
        u8* src8 = m3MemData(_mem) + operand;           \
        SRC_TYPE value;                                 \
        memcpy(&value, src8, sizeof(value));            \
        M3_BSWAP_##SRC_TYPE(value);                     \
        REG = (DEST_TYPE)value;                         \
        nextOp ();                                      \
    } else d_outOfBounds;                               \
}                                                       \
d_m3Op(DEST_TYPE##_Load_##SRC_TYPE##_s)                 \
{                                                       \
    u64 operand = slot (u32);                           \
    u32 offset = immediate (u32);                       \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (SRC_TYPE) <= _mem->length     \
    )) {                                                \
        u8* src8 = m3MemData(_mem) + operand;           \
        SRC_TYPE value;                                 \
        memcpy(&value, src8, sizeof(value));            \
        M3_BSWAP_##SRC_TYPE(value);                     \
        REG = (DEST_TYPE)value;                         \
        nextOp ();                                      \
    } else d_outOfBounds;                               \
}

//  printf ("get: %d -> %d\n", operand + offset, (i64) REG);


#define d_m3Load_i(DEST_TYPE, SRC_TYPE) d_m3Load(_r0, DEST_TYPE, SRC_TYPE)
#define d_m3Load_f(DEST_TYPE, SRC_TYPE) d_m3Load(_fp0, DEST_TYPE, SRC_TYPE)

#if d_m3HasFloat
d_m3Load_f (f32, f32);
d_m3Load_f (f64, f64);
#endif

d_m3Load_i (i32, i8);
d_m3Load_i (i32, u8);
d_m3Load_i (i32, i16);
d_m3Load_i (i32, u16);
d_m3Load_i (i32, i32);

d_m3Load_i (i64, i8);
d_m3Load_i (i64, u8);
d_m3Load_i (i64, i16);
d_m3Load_i (i64, u16);
d_m3Load_i (i64, i32);
d_m3Load_i (i64, u32);
d_m3Load_i (i64, i64);

#define d_m3Store(REG, SRC_TYPE, DEST_TYPE)             \
d_m3Op  (SRC_TYPE##_Store_##DEST_TYPE##_rs)             \
{                                                       \
    u64 operand = slot (u32);                           \
    u32 offset = immediate (u32);                       \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (DEST_TYPE) <= _mem->length    \
    )) {                                                \
        u8* mem8 = m3MemData(_mem) + operand;           \
        DEST_TYPE val = (DEST_TYPE) REG;                \
        M3_BSWAP_##DEST_TYPE(val);                      \
        memcpy(mem8, &val, sizeof(val));                \
        nextOp ();                                      \
    } else d_outOfBounds;                               \
}                                                       \
d_m3Op  (SRC_TYPE##_Store_##DEST_TYPE##_sr)             \
{                                                       \
    const SRC_TYPE value = slot (SRC_TYPE);             \
    u64 operand = (u32) _r0;                            \
    u32 offset = immediate (u32);                       \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (DEST_TYPE) <= _mem->length    \
    )) {                                                \
        u8* mem8 = m3MemData(_mem) + operand;           \
        DEST_TYPE val = (DEST_TYPE) value;              \
        M3_BSWAP_##DEST_TYPE(val);                      \
        memcpy(mem8, &val, sizeof(val));                \
        nextOp ();                                      \
    } else d_outOfBounds;                               \
}                                                       \
d_m3Op  (SRC_TYPE##_Store_##DEST_TYPE##_ss)             \
{                                                       \
    const SRC_TYPE value = slot (SRC_TYPE);             \
    u64 operand = slot (u32);                           \
    u32 offset = immediate (u32);                       \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (DEST_TYPE) <= _mem->length    \
    )) {                                                \
        u8* mem8 = m3MemData(_mem) + operand;           \
        DEST_TYPE val = (DEST_TYPE) value;              \
        M3_BSWAP_##DEST_TYPE(val);                      \
        memcpy(mem8, &val, sizeof(val));                \
        nextOp ();                                      \
    } else d_outOfBounds;                               \
}

// both operands can be in regs when storing a float
#define d_m3StoreFp(REG, TYPE)                          \
d_m3Op  (TYPE##_Store_##TYPE##_rr)                      \
{                                                       \
    u64 operand = (u32) _r0;                            \
    u32 offset = immediate (u32);                       \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (TYPE) <= _mem->length         \
    )) {                                                \
        u8* mem8 = m3MemData(_mem) + operand;           \
        TYPE val = (TYPE) REG;                          \
        M3_BSWAP_##TYPE(val);                           \
        memcpy(mem8, &val, sizeof(val));                \
        nextOp ();                                      \
    } else d_outOfBounds;                               \
}


#define d_m3Store_i(SRC_TYPE, DEST_TYPE) d_m3Store(_r0, SRC_TYPE, DEST_TYPE)
#define d_m3Store_f(SRC_TYPE, DEST_TYPE) d_m3Store(_fp0, SRC_TYPE, DEST_TYPE) d_m3StoreFp (_fp0, SRC_TYPE);

#if d_m3HasFloat
d_m3Store_f (f32, f32)
d_m3Store_f (f64, f64)
#endif

d_m3Store_i (i32, u8)
d_m3Store_i (i32, i16)
d_m3Store_i (i32, i32)

d_m3Store_i (i64, u8)
d_m3Store_i (i64, i16)
d_m3Store_i (i64, i32)
d_m3Store_i (i64, i64)

#undef m3MemCheck

//---------------------------------------------------------------------------------------------------------------------
# if 0 //d_m3EnableOptimizations
//---------------------------------------------------------------------------------------------------------------------

    #define d_m3BinaryOpWith1_i(TYPE, NAME, OPERATION)  \
    d_m3Op(TYPE##_##NAME)                               \
    {                                                   \
        _r0 = _r0 OPERATION 1;                          \
        nextOp ();                                      \
    }

    d_m3BinaryOpWith1_i (u64, Increment,    +)
    d_m3BinaryOpWith1_i (u32, Decrement,    -)

    d_m3BinaryOpWith1_i (u32, ShiftLeft1,   <<)
    d_m3BinaryOpWith1_i (u64, ShiftLeft1,   <<)

    d_m3BinaryOpWith1_i (u32, ShiftRight1,  >>)
    d_m3BinaryOpWith1_i (u64, ShiftRight1,  >>)

//---------------------------------------------------------------------------------------------------------------------
# endif


//---------------------------------------------------------------------------------------------------------------------
// debug/profiling
//---------------------------------------------------------------------------------------------------------------------
#if d_m3EnableOpTracing
d_m3RetSig  debugOp  (d_m3OpSig, cstr_t i_opcode)
{
    char name [100];
    strcpy (name, strstr (i_opcode, "op_") + 3);
    char * bracket = strstr (name, "(");
    if (bracket) {
        *bracket  = 0;
    }

    puts (name);
    nextOpDirect();
}
# endif

# if d_m3EnableOpProfiling

typedef struct M3ProfilerSlot
{
    cstr_t      opName;
    u64         hitCount;
}
M3ProfilerSlot;

void  ProfileHit  (cstr_t i_operationName);

d_m3RetSig  profileOp  (d_m3OpSig, cstr_t i_operationName)
{
    ProfileHit (i_operationName);

    nextOpDirect();
}
# endif

d_m3EndExternC

#endif // m3_exec_h
