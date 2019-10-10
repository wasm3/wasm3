#include <stdint.h>

#define NULL (0)

typedef double          f64;
typedef float           f32;
typedef uint64_t        u64;
typedef int64_t         i64;
typedef uint32_t        u32;
typedef int32_t         i32;
typedef uint16_t        u16;
typedef int16_t         i16;
typedef uint8_t         u8;
typedef int8_t          i8;


typedef i64             m3reg_t;
typedef void /*const*/ *                    code_t;
typedef code_t const * /*__restrict__*/        pc_t;
typedef const void *            m3ret_t;

#    define vectorcall

#    define d_m3OpSig                 pc_t _pc, u64 * _sp, u8 * _mem, m3reg_t _r0, f64 _fp0
#    define d_m3OpArgs                _sp, _mem, _r0, _fp0
#    define d_m3OpAllArgs             _pc, _sp, _mem, _r0, _fp0
#    define d_m3OpDefaultArgs         666, 666.0

typedef m3ret_t (vectorcall * IM3Operation) (d_m3OpSig);

# define immediate(TYPE)             * ((TYPE *) _pc++)
# define slot(TYPE)                  * (TYPE *) (_sp + immediate (i32))


# define d_m3RetSig                  static inline m3ret_t vectorcall
# define d_m3Op(NAME)                d_m3RetSig op_##NAME (d_m3OpSig)


# define nextOp()                    ((IM3Operation)(* _pc))(_pc + 1, d_m3OpArgs)


#define d_m3CommutativeOpMacro(RES, REG, TYPE, NAME, OP, ...) \
d_m3Op(TYPE##_##NAME##_sr)                                \
{                                                         \
    TYPE * stack = (TYPE *) (_sp + immediate (i32));      \
    OP((RES), (* stack), ((TYPE) REG), ##__VA_ARGS__);    \
    return nextOp ();                                     \
}                                                         \
d_m3Op(TYPE##_##NAME##_ss)                                \
{                                                         \
    TYPE * stackB = (TYPE *) (_sp + immediate (i32));     \
    TYPE * stackA = (TYPE *) (_sp + immediate (i32));     \
    OP((RES), (* stackA), (* stackB), ##__VA_ARGS__);     \
    return nextOp ();                                     \
}

#define d_m3OpMacro(RES, REG, TYPE, NAME, OP, ...)        \
d_m3Op(TYPE##_##NAME##_rs)                                \
{                                                         \
    TYPE * stack = (TYPE *) (_sp + immediate (i32));      \
    OP((RES), (* stack), ((TYPE) REG), ##__VA_ARGS__);    \
    return nextOp ();                                     \
}                                                         \
d_m3CommutativeOpMacro(RES, REG, TYPE,NAME, OP, ##__VA_ARGS__)

// Accept macros
#define d_m3CommutativeOpMacro_i(TYPE, NAME, MACRO, ...)     d_m3CommutativeOpMacro    ( _r0,  _r0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3OpMacro_i(TYPE, NAME, MACRO, ...)                d_m3OpMacro                ( _r0,  _r0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3CommutativeOpMacro_f(TYPE, NAME, MACRO, ...)     d_m3CommutativeOpMacro    (_fp0, _fp0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3OpMacro_f(TYPE, NAME, MACRO, ...)                d_m3OpMacro                (_fp0, _fp0, TYPE, NAME, MACRO, ##__VA_ARGS__)

#define M3_FUNC(RES, A, B, OP)  (RES) = OP((A), (B))    // Accept functions: res = OP(a,b)
#define M3_OPER(RES, A, B, OP)  (RES) = ((A) OP (B))    // Accept operators: res = a OP b

#define d_m3CommutativeOpFunc_i(TYPE, NAME, OP)     d_m3CommutativeOpMacro_i    (TYPE, NAME, M3_FUNC, OP)
#define d_m3OpFunc_i(TYPE, NAME, OP)                d_m3OpMacro_i                (TYPE, NAME, M3_FUNC, OP)
#define d_m3CommutativeOpFunc_f(TYPE, NAME, OP)     d_m3CommutativeOpMacro_f    (TYPE, NAME, M3_FUNC, OP)
#define d_m3OpFunc_f(TYPE, NAME, OP)                d_m3OpMacro_f                (TYPE, NAME, M3_FUNC, OP)

#define d_m3CommutativeOp_i(TYPE, NAME, OP)         d_m3CommutativeOpMacro_i    (TYPE, NAME, M3_OPER, OP)
#define d_m3Op_i(TYPE, NAME, OP)                    d_m3OpMacro_i                (TYPE, NAME, M3_OPER, OP)
#define d_m3CommutativeOp_f(TYPE, NAME, OP)         d_m3CommutativeOpMacro_f    (TYPE, NAME, M3_OPER, OP)
#define d_m3Op_f(TYPE, NAME, OP)                    d_m3OpMacro_f                (TYPE, NAME, M3_OPER, OP)


d_m3CommutativeOp_i (i32, Equal,                        ==)
d_m3CommutativeOp_i (i64, Equal,                        ==)
d_m3Op_i (i32, NotEqual,                    !=)
d_m3Op_i (i64, NotEqual,                    !=)


typedef struct M3OpInfo
{
    IM3Operation            operation_sr;        // top operand in register
    IM3Operation            operation_rs;        // top operand in stack
    IM3Operation            operation_ss;        // both operands in stack
}
M3OpInfo;

#define d_emptyOpList() NULL, NULL, NULL
#define d_unaryOpList(TYPE, NAME) op_##TYPE##_##NAME##_r, op_##TYPE##_##NAME##_s, NULL
#define d_binOpList(TYPE, NAME) op_##TYPE##_##NAME##_sr, op_##TYPE##_##NAME##_rs, op_##TYPE##_##NAME##_ss
#define d_commutativeBinOpList(TYPE, NAME) op_##TYPE##_##NAME##_sr, NULL, op_##TYPE##_##NAME##_ss


M3OpInfo c_operations[] = {
    { d_commutativeBinOpList (i32, Equal) },
    { d_binOpList (i32, NotEqual) },
    { d_commutativeBinOpList (i64, Equal) },
    { d_binOpList (i64, NotEqual) },
};
