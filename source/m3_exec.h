//
//  m3_exec.h
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.


#ifndef m3_exec_h
#define m3_exec_h

// All these functions could move over to the .c at some point. normally, I'd say screw it,
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

#ifndef M3_COMPILE_OPCODES
#  error "Opcodes should only be included in one compilation unit"
#endif

#include "m3_math_utils.h"
#include "m3_compile.h"
#include "m3_env.h"
#include "m3_info.h"
#include "m3_exec_defs.h"

#include <limits.h>

d_m3BeginExternC

#define rewrite_op(OP)             * ((void **) (_pc-1)) = (void*)(OP)

#define immediate(TYPE)            * ((TYPE *) _pc++)
#define skip_immediate(TYPE)       (_pc++)

#define slot(TYPE)                 * (TYPE *) (_sp + immediate (i32))
#define slot_ptr(TYPE)             (TYPE *) (_sp + immediate (i32))

// Reads one address, index or length operand from a slot, at the width the
// memory or table it belongs to is addressed by. Either way exactly one slot
// immediate is consumed; only how much of the slot is read differs.
#define d_m3WideOperand(IS64)      ((IS64) ? slot (u64) : (u64) slot (u32))

// Whether a range fits a memory, written so that it holds for operands of any
// magnitude: the subtraction cannot wrap once the start is known to be within
// the memory, where start + length could.
#define d_m3MemRangeOk(START, LENGTH, MEM)  \
   ((START) <= (MEM)->length and (LENGTH) <= (MEM)->length - (START))


#if d_m3EnableOpProfiling
d_m3RetSig profileOp (d_m3OpSig, cstr_t i_operationName);
#  define nextOp()                 M3_MUSTTAIL return profileOp (d_m3OpAllArgs, __FUNCTION__)
#elif d_m3EnableOpTracing
d_m3RetSig debugOp (d_m3OpSig, cstr_t i_operationName);
#  define nextOp()                 M3_MUSTTAIL return debugOp (d_m3OpAllArgs, __FUNCTION__)
#else
#  define nextOp()                 nextOpDirect()
#endif

// Next-operation preloading. The dispatch load and the indirect jump that consumes it
// sit next to each other, so the jump cannot resolve until the load returns. An
// operation's _pc is final as soon as it has taken its immediates -- the offsets are
// compile-time constants -- so the load can be issued there instead, leaving the
// operation's own work as cover for its latency.
//
// Preloading is dropped in profiling and tracing builds, where dispatch goes through a
// hook instead and the loaded pointer would be the wrong thing to call.
#if d_m3PreloadNextOp && !(d_m3EnableOpProfiling || d_m3EnableOpTracing)
#  define d_m3PreloadNext()        const IM3Operation _nextOp = (IM3Operation) (* _pc)
#  define nextOpPreloaded()        M3_MUSTTAIL return _nextOp (_pc + 1, d_m3OpArgs)
#else
#  define d_m3PreloadNext()
#  define nextOpPreloaded()        nextOp()
#endif

#if d_m3EnableOpProfiling
d_m3RetSig profileJumpOp (d_m3OpSig, cstr_t i_operationName);
// An operation that leaves by a branch never reaches nextOp, so without this it is
// absent from the profile entirely and its outgoing edges are attributed to whatever
// ran before it. Has to stay one statement: jumpOp appears in brace-less else arms.
#  define jumpOp(PC)               M3_MUSTTAIL return profileJumpOp ((pc_t)(PC), d_m3OpArgs, __FUNCTION__)
#else
#  define jumpOp(PC)               jumpOpDirect(PC)
#endif

#if d_m3RecordBacktraces
#  define pushBacktraceFrame()            (PushBacktraceFrame (_mem->runtime, _pc - 1))
#  define fillBacktraceFrame(FUNCTION)    (FillBacktraceFunctionInfo (_mem->runtime, function))

#  define newTrap(err)                    return (pushBacktraceFrame (), err)
#  define forwardTrap(err)                return err
#else
#  define pushBacktraceFrame()            do {} while (0)
#  define fillBacktraceFrame(FUNCTION)    do {} while (0)

#  define newTrap(err)                    return err
#  define forwardTrap(err)                return err
#endif

// Trap before an operation that claims a native frame of its own would drive
// the native C stack past runtime->stackLimit. Reads the current stack pointer
// via a frame-address probe and compares against the low-water mark set by
// d_m3StackLimitEnter. Every op that keeps a frame across an inner dispatch
// probes here first -- op_Call and op_CallIndirect, and also op_Loop and
// op_TryTable, whose frames outlive the body they run. Guarding the frame
// claim rather than the call is what keeps op_ReturnCall bounded: the tail
// call itself adds nothing, but the frames it leaves standing behind it do.
#if d_m3MaxNativeStack > 0
#  define d_m3CheckNativeStack()                                               \
       do {                                                                    \
           void * _m3Limit = m3MemRuntime (_mem)->stackLimit;                  \
           if (M3_UNLIKELY (_m3Limit && m3_NativeStackPtr () < _m3Limit))      \
               newTrap (m3Err_trapStackOverflow);                              \
       } while (0)
#else
#  define d_m3CheckNativeStack()           do {} while (0)
#endif


#if d_m3HasStackSwitching

// Calling into compiled code with an explicit register pair, which the replay
// needs and the d_m3OpDefaultArgs spelling cannot express.
#  if d_m3HasFloat
#    define d_m3ExpRegArgs(R0, FP0)        (R0), (FP0)
#  else
#    define d_m3ExpRegArgs(R0, FP0)        (R0)
#  endif

#  if (d_m3EnableOpProfiling || d_m3EnableOpTracing)
#    define d_m3CallWithRegs(PC, SP, MEM, R0, FP0)                             \
         Call ((PC), (SP), (MEM), d_m3ExpRegArgs (R0, FP0), d_m3BaseCstr)
#  else
#    define d_m3CallWithRegs(PC, SP, MEM, R0, FP0)                             \
         Call ((PC), (SP), (MEM), d_m3ExpRegArgs (R0, FP0))
#  endif

// A suspend is on its way up the native stack and this call was holding a
// frame: record what it takes to stand that frame back up, then keep unwinding
// with whatever the recording says. The registers go in because op_Call
// resumes the caller with the ones its own frame was holding.
//
// Out of line, and deliberately so: op_Call and its two siblings are the
// deepest-recursing functions in the interpreter, and a whole M3Frame of
// locals on every call is stack the recursion cannot spare - least of all in a
// sanitizer build, where each local carries a redzone.
//
// Called ahead of pushBacktraceFrame: a suspend is not a trap and has no
// business writing a backtrace.
static M3_NOINLINE
m3ret_t RecordCallFrame (IM3Runtime i_runtime, pc_t i_pc, m3stack_t i_sp, IM3Memory i_memory,
                         IM3Function i_function, m3reg_t i_r0
#  if d_m3HasFloat
                         ,
                         f64 i_fp0
#  endif
)
{
    M3Frame frame;

    frame.kind = frame_call;
    frame.pc = i_pc;
    frame.sp = i_sp;
    frame.memory = i_memory;
    frame.call.function = i_function;
    frame.call.r0 = i_r0;
#  if d_m3HasFloat
    frame.call.fp0 = i_fp0;
#  endif

    return Continuation_RecordFrame(i_runtime, &frame);
}

#  define d_m3RecordCallFrame(MEMORY, FUNCTION)                                \
       if (M3_UNLIKELY (r == m3Err_continuationSuspended)) {                   \
           return RecordCallFrame (m3MemRuntime (_mem), _pc, _sp, (MEMORY),    \
                                   (FUNCTION), d_m3ExpRegArgs (_r0, _fp0));    \
       }


// op_Loop and op_TryTable hold native frames across their bodies and nest as
// deeply as the Wasm does, so they keep their frames out of this too.
static M3_NOINLINE
m3ret_t RecordLoopFrame (IM3Runtime i_runtime, pc_t i_pc, m3stack_t i_sp, IM3Memory i_memory)
{
    M3Frame frame;

    frame.kind = frame_loop;
    frame.pc = i_pc;
    frame.sp = i_sp;
    frame.memory = i_memory;

    return Continuation_RecordFrame(i_runtime, &frame);
}


#  if d_m3HasExceptionHandling
static M3_NOINLINE
m3ret_t RecordTryFrame (IM3Runtime i_runtime, pc_t i_clauses, m3stack_t i_sp, IM3Memory i_memory,
                        u32 i_numClauses, bool i_handlersLive)
{
    M3Frame frame;

    frame.kind = frame_try;
    frame.pc = i_clauses;
    frame.sp = i_sp;
    frame.memory = i_memory;
    frame.try_.numClauses = i_numClauses;
    frame.try_.handlersLive = i_handlersLive;

    return Continuation_RecordFrame(i_runtime, &frame);
}
#  endif


#  if d_m3EntryKeepsFrame
static M3_NOINLINE
m3ret_t RecordEntryFrame (IM3Runtime i_runtime, pc_t i_pc, m3stack_t i_sp, IM3Memory i_memory,
                          IM3Function i_function)
{
    M3Frame frame;

    frame.kind = frame_entry;
    frame.pc = i_pc;
    frame.sp = i_sp;
    frame.memory = i_memory;
    frame.entry.function = i_function;

    return Continuation_RecordFrame(i_runtime, &frame);
}
#  endif
#else
#  define d_m3RecordCallFrame(MEMORY, FUNCTION)
#endif


#if d_m3EnableStrace == 1
// Flat trace
#  define d_m3TracePrepare
#  define d_m3TracePrint(fmt, ...)            fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#elif d_m3EnableStrace >= 2
// Structured trace
#  define d_m3TracePrepare                    const IM3Runtime trace_rt = m3MemRuntime(_mem); (void) trace_rt;
#  define d_m3TracePrint(fmt, ...)            fprintf(stderr, "%*s" fmt "\n", (trace_rt->callDepth)*2, "", ##__VA_ARGS__)
#else
#  define d_m3TracePrepare
#  define d_m3TracePrint(fmt, ...)
#endif

#if d_m3EnableStrace >= 3
#  define d_m3TraceLoad(TYPE,offset,val)      d_m3TracePrint("load." #TYPE "  0x%x = %" PRI##TYPE, offset, val)
#  define d_m3TraceStore(TYPE,offset,val)     d_m3TracePrint("store." #TYPE " 0x%x , %" PRI##TYPE, offset, val)
#else
#  define d_m3TraceLoad(TYPE,offset,val)
#  define d_m3TraceStore(TYPE,offset,val)
#endif

#ifdef DEBUG
  #define d_outOfBounds newTrap (ErrorRuntime (m3Err_trapOutOfBoundsMemoryAccess,   \
                        _mem->runtime, "memory size: %zu; access offset: %zu",      \
                        _mem->length, operand))

#  define d_outOfBoundsMemOp(OFFSET, SIZE) newTrap (ErrorRuntime (m3Err_trapOutOfBoundsMemoryAccess,   \
                     _mem->runtime, "memory size: %zu; access offset: %zu; size: %u",     \
                     _mem->length, OFFSET, SIZE))
#else
  #define d_outOfBounds newTrap (m3Err_trapOutOfBoundsMemoryAccess)

#  define d_outOfBoundsMemOp(OFFSET, SIZE) newTrap (m3Err_trapOutOfBoundsMemoryAccess)

#endif

#if (d_m3EnableOpProfiling || d_m3EnableOpTracing)
d_m3RetSig Call (d_m3OpSig, cstr_t i_operationName)
#else
d_m3RetSig Call (d_m3OpSig)
#endif
{
    m3ret_t possible_trap = m3_Yield();
    if (M3_UNLIKELY(possible_trap)) {
        return possible_trap;
    }

    nextOpDirect();
}

#define d_m3CommutativeOpMacro(RES, REG, TYPE, NAME, OP, ...) \
d_m3Op(TYPE##_##NAME##_rs)                              \
{                                                       \
    TYPE operand = slot (TYPE);                         \
    d_m3PreloadNext ();                                 \
    OP((RES), operand, ((TYPE) REG), ##__VA_ARGS__);    \
    nextOpPreloaded ();                                 \
}                                                       \
d_m3Op(TYPE##_##NAME##_ss)                              \
{                                                       \
    TYPE operand2 = slot (TYPE);                        \
    TYPE operand1 = slot (TYPE);                        \
    d_m3PreloadNext ();                                 \
    OP((RES), operand1, operand2, ##__VA_ARGS__);       \
    nextOpPreloaded ();                                 \
}

#define d_m3OpMacro(RES, REG, TYPE, NAME, OP, ...)      \
d_m3Op(TYPE##_##NAME##_sr)                              \
{                                                       \
    TYPE operand = slot (TYPE);                         \
    d_m3PreloadNext ();                                 \
    OP((RES), ((TYPE) REG), operand, ##__VA_ARGS__);    \
    nextOpPreloaded ();                                 \
}                                                       \
d_m3CommutativeOpMacro(RES, REG, TYPE,NAME, OP, ##__VA_ARGS__)

// Accept macros
#define d_m3CommutativeOpMacro_i(TYPE, NAME, MACRO, ...)    d_m3CommutativeOpMacro  ( _r0,  _r0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3OpMacro_i(TYPE, NAME, MACRO, ...)               d_m3OpMacro             ( _r0,  _r0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3CommutativeOpMacro_f(TYPE, NAME, MACRO, ...)    d_m3CommutativeOpMacro  (_fp0, _fp0, TYPE, NAME, MACRO, ##__VA_ARGS__)
#define d_m3OpMacro_f(TYPE, NAME, MACRO, ...)               d_m3OpMacro             (_fp0, _fp0, TYPE, NAME, MACRO, ##__VA_ARGS__)

#define M3_FUNC(RES, A, B, OP)  (RES) = OP((A), (B))        // Accept functions: res = OP(a,b)
#define M3_OPER(RES, A, B, OP)  (RES) = ((A) OP (B))        // Accept operators: res = a OP b

// The same two, for a float operation whose NaN result the spec leaves to the host:
// CANON is the canonicalizer for the operation's width (only in effect when d_m3CanonicalNaN is set).
#define M3_FUNC_A(RES, A, B, OP, CANON)  (RES) = CANON(OP((A), (B)))
#define M3_OPER_A(RES, A, B, OP, CANON)  (RES) = CANON((A) OP (B))

#define d_m3CommutativeOpFunc_i(TYPE, NAME, OP)     d_m3CommutativeOpMacro_i    (TYPE, NAME, M3_FUNC, OP)
#define d_m3OpFunc_i(TYPE, NAME, OP)                d_m3OpMacro_i               (TYPE, NAME, M3_FUNC, OP)
#define d_m3CommutativeOpFunc_f(TYPE, NAME, OP)     d_m3CommutativeOpMacro_f    (TYPE, NAME, M3_FUNC, OP)
#define d_m3OpFunc_f(TYPE, NAME, OP)                d_m3OpMacro_f               (TYPE, NAME, M3_FUNC, OP)

#define d_m3CommutativeOp_i(TYPE, NAME, OP)         d_m3CommutativeOpMacro_i    (TYPE, NAME, M3_OPER, OP)
#define d_m3Op_i(TYPE, NAME, OP)                    d_m3OpMacro_i               (TYPE, NAME, M3_OPER, OP)
#define d_m3CommutativeOp_f(TYPE, NAME, OP)         d_m3CommutativeOpMacro_f    (TYPE, NAME, M3_OPER, OP)
#define d_m3Op_f(TYPE, NAME, OP)                    d_m3OpMacro_f               (TYPE, NAME, M3_OPER, OP)

// ...and the arithmetic ones, which name the canonicalizer for their own width
#define d_m3CommutativeArithOp_f(TYPE, NAME, OP)    d_m3CommutativeOpMacro_f    (TYPE, NAME, M3_OPER_A, OP, canon_nan_##TYPE)
#define d_m3ArithOp_f(TYPE, NAME, OP)               d_m3OpMacro_f               (TYPE, NAME, M3_OPER_A, OP, canon_nan_##TYPE)
#define d_m3ArithOpFunc_f(TYPE, NAME, OP)           d_m3OpMacro_f               (TYPE, NAME, M3_FUNC_A, OP, canon_nan_##TYPE)

// compare needs to be distinct for fp 'cause the result must be _r0
#define d_m3CompareOp_f(TYPE, NAME, OP)             d_m3OpMacro                 (_r0, _fp0, TYPE, NAME, M3_OPER, OP)
#define d_m3CommutativeCmpOp_f(TYPE, NAME, OP)      d_m3CommutativeOpMacro      (_r0, _fp0, TYPE, NAME, M3_OPER, OP)


//-----------------------
// clang-format off

// signed
d_m3CommutativeOp_i(i32, Equal,            ==)     d_m3CommutativeOp_i(i64, Equal,            ==)
d_m3CommutativeOp_i(i32, NotEqual,         !=)     d_m3CommutativeOp_i(i64, NotEqual,         !=)

d_m3Op_i(i32, LessThan,                    < )     d_m3Op_i(i64, LessThan,                    < )
d_m3Op_i(i32, GreaterThan,                 > )     d_m3Op_i(i64, GreaterThan,                 > )
d_m3Op_i(i32, LessThanOrEqual,             <=)     d_m3Op_i(i64, LessThanOrEqual,             <=)
d_m3Op_i(i32, GreaterThanOrEqual,          >=)     d_m3Op_i(i64, GreaterThanOrEqual,          >=)

// unsigned
d_m3Op_i(u32, LessThan,                    < )     d_m3Op_i(u64, LessThan,                    < )
d_m3Op_i(u32, GreaterThan,                 > )     d_m3Op_i(u64, GreaterThan,                 > )
d_m3Op_i(u32, LessThanOrEqual,             <=)     d_m3Op_i(u64, LessThanOrEqual,             <=)
d_m3Op_i(u32, GreaterThanOrEqual,          >=)     d_m3Op_i(u64, GreaterThanOrEqual,          >=)

#if d_m3HasFloat
d_m3CommutativeCmpOp_f(f32, Equal,         ==)     d_m3CommutativeCmpOp_f(f64, Equal,         ==)
d_m3CommutativeCmpOp_f(f32, NotEqual,      !=)     d_m3CommutativeCmpOp_f(f64, NotEqual,      !=)
d_m3CompareOp_f(f32, LessThan,             < )     d_m3CompareOp_f(f64, LessThan,             < )
d_m3CompareOp_f(f32, GreaterThan,          > )     d_m3CompareOp_f(f64, GreaterThan,          > )
d_m3CompareOp_f(f32, LessThanOrEqual,      <=)     d_m3CompareOp_f(f64, LessThanOrEqual,      <=)
d_m3CompareOp_f(f32, GreaterThanOrEqual,   >=)     d_m3CompareOp_f(f64, GreaterThanOrEqual,   >=)
#endif

#define OP_ADD_32(A,B) (i32)((u32)(A) + (u32)(B))
#define OP_ADD_64(A,B) (i64)((u64)(A) + (u64)(B))
#define OP_SUB_32(A,B) (i32)((u32)(A) - (u32)(B))
#define OP_SUB_64(A,B) (i64)((u64)(A) - (u64)(B))
#define OP_MUL_32(A,B) (i32)((u32)(A) * (u32)(B))
#define OP_MUL_64(A,B) (i64)((u64)(A) * (u64)(B))

d_m3CommutativeOpFunc_i (i32, Add,      OP_ADD_32)  d_m3CommutativeOpFunc_i (i64, Add,      OP_ADD_64)
d_m3CommutativeOpFunc_i (i32, Multiply, OP_MUL_32)  d_m3CommutativeOpFunc_i (i64, Multiply, OP_MUL_64)

d_m3OpFunc_i (i32, Subtract,            OP_SUB_32)  d_m3OpFunc_i (i64, Subtract,            OP_SUB_64)

#define OP_SHL_32(X,N) ((X) << ((u32)(N) % 32))
#define OP_SHL_64(X,N) ((X) << ((u64)(N) % 64))
#define OP_SHR_32(X,N) ((X) >> ((u32)(N) % 32))
#define OP_SHR_64(X,N) ((X) >> ((u64)(N) % 64))

d_m3OpFunc_i (u32, ShiftLeft,       OP_SHL_32)      d_m3OpFunc_i (u64, ShiftLeft,       OP_SHL_64)
d_m3OpFunc_i (i32, ShiftRight,      OP_SHR_32)      d_m3OpFunc_i (i64, ShiftRight,      OP_SHR_64)
d_m3OpFunc_i (u32, ShiftRight,      OP_SHR_32)      d_m3OpFunc_i (u64, ShiftRight,      OP_SHR_64)

d_m3CommutativeOp_i(u32, And,              &)
d_m3CommutativeOp_i(u32, Or,               |)
d_m3CommutativeOp_i(u32, Xor,              ^)

d_m3CommutativeOp_i(u64, And,              &)
d_m3CommutativeOp_i(u64, Or,               |)
d_m3CommutativeOp_i(u64, Xor,              ^)

#if d_m3FoldSetLocal

// destination-folded ("_f") variants: same operand shapes as _rs/_sr/_ss, but the result is
// written to a trailing destination slot instead of _r0 - a producer fused with its local.set.
// operand immediates keep their normal order; the destination slot is the last immediate.
// The destination immediate is taken as a pointer before the operation runs, rather
// than written through at the end: that leaves _pc final early enough for the dispatch
// load to be issued ahead of the arithmetic instead of after it.
// The result is written through to the register as well as to the destination slot.
// Destination folding only ever replaces an operation that computed into the register,
// so the old value there is already dead and keeping the result costs nothing -- and it
// lets a local.tee carry on in the register instead of reloading the slot it just wrote.
#  define d_m3FoldCommutativeOpMacro(REG, TYPE, NAME, OP, ...) \
d_m3Op(TYPE##_##NAME##_rs_f)                            \
{                                                       \
    TYPE operand = slot (TYPE);                         \
    TYPE * dest = slot_ptr (TYPE);                      \
    d_m3PreloadNext ();                                 \
    TYPE result;                                        \
    OP((result), operand, ((TYPE) REG), ##__VA_ARGS__); \
    * dest = result;  REG = result;                     \
    nextOpPreloaded ();                                 \
}                                                       \
d_m3Op(TYPE##_##NAME##_ss_f)                            \
{                                                       \
    TYPE operand2 = slot (TYPE);                        \
    TYPE operand1 = slot (TYPE);                        \
    TYPE * dest = slot_ptr (TYPE);                      \
    d_m3PreloadNext ();                                 \
    TYPE result;                                        \
    OP((result), operand1, operand2, ##__VA_ARGS__);    \
    * dest = result;  REG = result;                     \
    nextOpPreloaded ();                                 \
}

#  define d_m3FoldOpMacro(REG, TYPE, NAME, OP, ...)     \
d_m3Op(TYPE##_##NAME##_sr_f)                            \
{                                                       \
    TYPE operand = slot (TYPE);                         \
    TYPE * dest = slot_ptr (TYPE);                      \
    d_m3PreloadNext ();                                 \
    TYPE result;                                        \
    OP((result), ((TYPE) REG), operand, ##__VA_ARGS__); \
    * dest = result;  REG = result;                     \
    nextOpPreloaded ();                                 \
}                                                       \
d_m3FoldCommutativeOpMacro(REG, TYPE, NAME, OP, ##__VA_ARGS__)

// A commutative operator has no _sr form in its variant table, so the fold set skips
// the _sr_f handler it could never be patched to.
#define d_m3FoldOp_i(TYPE, NAME, OP, ...)               d_m3FoldOpMacro             ( _r0, TYPE, NAME, OP, ##__VA_ARGS__)
#define d_m3FoldOp_f(TYPE, NAME, OP, ...)               d_m3FoldOpMacro             (_fp0, TYPE, NAME, OP, ##__VA_ARGS__)
#define d_m3FoldCommutativeOp_i(TYPE, NAME, OP, ...)    d_m3FoldCommutativeOpMacro  ( _r0, TYPE, NAME, OP, ##__VA_ARGS__)
#define d_m3FoldCommutativeOp_f(TYPE, NAME, OP, ...)    d_m3FoldCommutativeOpMacro  (_fp0, TYPE, NAME, OP, ##__VA_ARGS__)

d_m3FoldCommutativeOp_i(i32, Add,          M3_FUNC, OP_ADD_32)
d_m3FoldOp_i           (i32, Subtract,     M3_FUNC, OP_SUB_32)
d_m3FoldCommutativeOp_i(i32, Multiply,     M3_FUNC, OP_MUL_32)
d_m3FoldCommutativeOp_i(u32, And,          M3_OPER, &)
d_m3FoldCommutativeOp_i(u32, Or,           M3_OPER, |)
d_m3FoldCommutativeOp_i(u32, Xor,          M3_OPER, ^)
d_m3FoldOp_i           (u32, ShiftLeft,    M3_FUNC, OP_SHL_32)
d_m3FoldOp_i           (i32, ShiftRight,   M3_FUNC, OP_SHR_32)
d_m3FoldOp_i           (u32, ShiftRight,   M3_FUNC, OP_SHR_32)

d_m3FoldCommutativeOp_i(i64, Add,          M3_FUNC, OP_ADD_64)
d_m3FoldOp_i           (i64, Subtract,     M3_FUNC, OP_SUB_64)
d_m3FoldCommutativeOp_i(i64, Multiply,     M3_FUNC, OP_MUL_64)
d_m3FoldCommutativeOp_i(u64, And,          M3_OPER, &)
d_m3FoldCommutativeOp_i(u64, Or,           M3_OPER, |)
d_m3FoldCommutativeOp_i(u64, Xor,          M3_OPER, ^)
d_m3FoldOp_i           (u64, ShiftLeft,    M3_FUNC, OP_SHL_64)
d_m3FoldOp_i           (i64, ShiftRight,   M3_FUNC, OP_SHR_64)
d_m3FoldOp_i           (u64, ShiftRight,   M3_FUNC, OP_SHR_64)

#  if d_m3HasFloat
// Without these a float result round-trips through _fp0 and a separate SetSlot, which
// on float-heavy code is about one dispatch in five.
d_m3FoldCommutativeOp_f(f32, Add,          M3_OPER_A, +, canon_nan_f32)
d_m3FoldOp_f           (f32, Subtract,     M3_OPER_A, -, canon_nan_f32)
d_m3FoldCommutativeOp_f(f32, Multiply,     M3_OPER_A, *, canon_nan_f32)
d_m3FoldOp_f           (f32, Divide,       M3_OPER_A, /, canon_nan_f32)
d_m3FoldCommutativeOp_f(f64, Add,          M3_OPER_A, +, canon_nan_f64)
d_m3FoldOp_f           (f64, Subtract,     M3_OPER_A, -, canon_nan_f64)
d_m3FoldCommutativeOp_f(f64, Multiply,     M3_OPER_A, *, canon_nan_f64)
d_m3FoldOp_f           (f64, Divide,       M3_OPER_A, /, canon_nan_f64)
#endif

#endif // d_m3FoldSetLocal

#if d_m3HasFloat
d_m3CommutativeArithOp_f(f32, Add,         +)      d_m3CommutativeArithOp_f(f64, Add,         +)
d_m3CommutativeArithOp_f(f32, Multiply,    *)      d_m3CommutativeArithOp_f(f64, Multiply,    *)
d_m3ArithOp_f(f32, Subtract,               -)      d_m3ArithOp_f(f64, Subtract,               -)
d_m3ArithOp_f(f32, Divide,                 /)      d_m3ArithOp_f(f64, Divide,                 /)
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
d_m3ArithOpFunc_f(f32, Min, min_f32);
d_m3ArithOpFunc_f(f32, Max, max_f32);
d_m3ArithOpFunc_f(f64, Min, min_f64);
d_m3ArithOpFunc_f(f64, Max, max_f64);

// copysign is defined bitwise and keeps its operand's payload - see canon_nan_f32
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
#define M3_UNARY_A(RES, X, OP, CANON) (RES) = CANON(OP(X))

#define d_m3UnaryOp_i(TYPE, NAME, OPERATION)        d_m3UnaryMacro( _r0,  _r0, TYPE, NAME, M3_UNARY, OPERATION)
#define d_m3UnaryOp_f(TYPE, NAME, OPERATION)        d_m3UnaryMacro(_fp0, _fp0, TYPE, NAME, M3_UNARY, OPERATION)
#define d_m3UnaryArithOp_f(TYPE, NAME, OPERATION)   d_m3UnaryMacro(_fp0, _fp0, TYPE, NAME, M3_UNARY_A, OPERATION, canon_nan_##TYPE)

#if d_m3HasFloat
// abs and neg are defined bitwise and keep their operand's payload - see canon_nan_f32
d_m3UnaryOp_f(f32, Abs,             fabsf);    d_m3UnaryOp_f(f64, Abs,             fabs);
d_m3UnaryArithOp_f(f32, Ceil,       ceilf);    d_m3UnaryArithOp_f(f64, Ceil,       ceil);
d_m3UnaryArithOp_f(f32, Floor,      floorf);   d_m3UnaryArithOp_f(f64, Floor,      floor);
d_m3UnaryArithOp_f(f32, Trunc,      truncf);   d_m3UnaryArithOp_f(f64, Trunc,      trunc);
d_m3UnaryArithOp_f(f32, Sqrt,       sqrtf);    d_m3UnaryArithOp_f(f64, Sqrt,       sqrt);
d_m3UnaryArithOp_f(f32, Nearest,    rintf);    d_m3UnaryArithOp_f(f64, Nearest,    rint);
#  if defined(M3_COMPILER_TCC)
d_m3UnaryOp_f(f32, Negate,     m3_negf);       d_m3UnaryOp_f(f64, Negate,     m3_neg);
#  else
d_m3UnaryOp_f(f32, Negate,     -);             d_m3UnaryOp_f(f64, Negate,     -);
#  endif
#endif

#define OP_EQZ(x) ((x) == 0)

d_m3UnaryOp_i(i32, EqualToZero, OP_EQZ)
d_m3UnaryOp_i(i64, EqualToZero, OP_EQZ)

// clz(0), ctz(0) results are undefined for rest platforms, fix it
#if (defined(__i386__) || defined(__x86_64__)) && !(defined(__AVX2__) || (defined(__ABM__) && defined(__BMI__)))
#define OP_CLZ_32(x) (M3_UNLIKELY((x) == 0) ? 32 : __builtin_clz(x))
#define OP_CTZ_32(x) (M3_UNLIKELY((x) == 0) ? 32 : __builtin_ctz(x))
// for 64-bit instructions branchless approach more preferable
#define OP_CLZ_64(x) (__builtin_clzll((x) | (1ULL <<  0)) + OP_EQZ(x))
#define OP_CTZ_64(x) (__builtin_ctzll((x) | (1ULL << 63)) + OP_EQZ(x))
#elif defined(__ppc__) || defined(__ppc64__)
// PowerPC is defined for __builtin_clz(0) and __builtin_ctz(0).
// See (https://github.com/aquynh/capstone/blob/master/MathExtras.h#L99)
#define OP_CLZ_32(x) __builtin_clz(x)
#define OP_CTZ_32(x) __builtin_ctz(x)
#define OP_CLZ_64(x) __builtin_clzll(x)
#define OP_CTZ_64(x) __builtin_ctzll(x)
#else
#define OP_CLZ_32(x) (M3_UNLIKELY((x) == 0) ? 32 : __builtin_clz(x))
#define OP_CTZ_32(x) (M3_UNLIKELY((x) == 0) ? 32 : __builtin_ctz(x))
#define OP_CLZ_64(x) (M3_UNLIKELY((x) == 0) ? 64 : __builtin_clzll(x))
#define OP_CTZ_64(x) (M3_UNLIKELY((x) == 0) ? 64 : __builtin_ctzll(x))
#endif

d_m3UnaryOp_i(u32, Clz, OP_CLZ_32)
d_m3UnaryOp_i(u64, Clz, OP_CLZ_64)

d_m3UnaryOp_i(u32, Ctz, OP_CTZ_32)
d_m3UnaryOp_i(u64, Ctz, OP_CTZ_64)

d_m3UnaryOp_i(u32, Popcnt, __builtin_popcount)
d_m3UnaryOp_i(u64, Popcnt, __builtin_popcountll)

#define OP_WRAP_I64(X) ((X) & 0x00000000ffffffff)

d_m3Op(i32_Wrap_i64_r)
{
    _r0 = OP_WRAP_I64((i64)_r0);
    nextOp();
}

d_m3Op(i32_Wrap_i64_s)
{
    i64 operand = slot(i64);
    _r0 = OP_WRAP_I64(operand);
    nextOp();
}

// Integer sign extension operations
#define OP_EXTEND8_S_I32(X)  ((int32_t)(int8_t)(X))
#define OP_EXTEND16_S_I32(X) ((int32_t)(int16_t)(X))
#define OP_EXTEND8_S_I64(X)  ((int64_t)(int8_t)(X))
#define OP_EXTEND16_S_I64(X) ((int64_t)(int16_t)(X))
#define OP_EXTEND32_S_I64(X) ((int64_t)(int32_t)(X))

d_m3UnaryOp_i(i32, Extend8_s,  OP_EXTEND8_S_I32)
d_m3UnaryOp_i(i32, Extend16_s, OP_EXTEND16_S_I32)
d_m3UnaryOp_i(i64, Extend8_s,  OP_EXTEND8_S_I64)
d_m3UnaryOp_i(i64, Extend16_s, OP_EXTEND16_S_I64)
d_m3UnaryOp_i(i64, Extend32_s, OP_EXTEND32_S_I64)

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
d_m3TypeModifyOp(_r0, _r0, i64, Extend, i32);
d_m3TypeModifyOp(_r0, _r0, i64, Extend, u32);

// Float to float. Narrowing or widening a NaN produces an arithmetic one, so these
// two go through the canonicalizer where the int conversions have nothing to do.
#define d_m3FloatModifyOp(TO, NAME, FROM)                   \
d_m3Op(TO##_##NAME##_##FROM##_r)                            \
{                                                           \
    _fp0 = canon_nan_##TO ((TO) ((FROM) _fp0));             \
    nextOp ();                                              \
}                                                           \
                                                            \
d_m3Op(TO##_##NAME##_##FROM##_s)                            \
{                                                           \
    FROM from = slot (FROM);                                \
    _fp0 = canon_nan_##TO ((TO) (from));                    \
    nextOp ();                                              \
}

#if d_m3HasFloat
d_m3FloatModifyOp(f32, Demote, f64);
d_m3FloatModifyOp(f64, Promote, f32);
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
d_m3TypeConvertOp(_fp0, _r0, f64, Convert, i32);
d_m3TypeConvertOp(_fp0, _r0, f64, Convert, u32);
d_m3TypeConvertOp(_fp0, _r0, f64, Convert, i64);
d_m3TypeConvertOp(_fp0, _r0, f64, Convert, u64);

d_m3TypeConvertOp(_fp0, _r0, f32, Convert, i32);
d_m3TypeConvertOp(_fp0, _r0, f32, Convert, u32);
d_m3TypeConvertOp(_fp0, _r0, f32, Convert, i64);
d_m3TypeConvertOp(_fp0, _r0, f32, Convert, u64);
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
d_m3ReinterpretOp(_r0, i32, _fp0, f32)
d_m3ReinterpretOp(_r0, i64, _fp0, f64)
d_m3ReinterpretOp(_fp0, f32, _r0, i32)
d_m3ReinterpretOp(_fp0, f64, _r0, i64)
#endif

// clang-format on

d_m3Op(GetGlobal_s32)
{
    u32* global = immediate(u32*);
    slot(u32) = *global;                        //  printf ("get global: %p %" PRIi64 "\n", global, *global);

    nextOp();
}


d_m3Op(GetGlobal_s64)
{
    u64* global = immediate(u64*);
    slot(u64) = *global;                        // printf ("get global: %p %" PRIi64 "\n", global, *global);

    nextOp();
}


d_m3Op(SetGlobal_i32)
{
    u32* global = immediate(u32*);
    *global = (u32)_r0;                         //  printf ("set global: %p %" PRIi64 "\n", global, _r0);

    nextOp();
}


d_m3Op(SetGlobal_i64)
{
    u64* global = immediate(u64*);
    *global = (u64)_r0;                         //  printf ("set global: %p %" PRIi64 "\n", global, _r0);

    nextOp();
}


d_m3Op(Call)
{
    d_m3CheckNativeStack();

    pc_t      callPC = immediate(pc_t);
    i32       stackOffset = immediate(i32);
    IM3Memory memory = m3MemInfo(_mem);

    m3stack_t sp = _sp + stackOffset;

#if (d_m3EnableOpProfiling || d_m3EnableOpTracing)
    m3ret_t r = Call(callPC, sp, _mem, d_m3OpDefaultArgs, d_m3BaseCstr);
#else
    m3ret_t r = Call(callPC, sp, _mem, d_m3OpDefaultArgs);
#endif

    _mem = memory->mallocated;

    if (M3_LIKELY(not r)) {
        nextOp();
    } else {
        d_m3RecordCallFrame(memory, NULL);
        pushBacktraceFrame();
        forwardTrap(r);
    }
}


// call_ref / return_call_ref: like the indirect calls, except the callee comes
// straight off the stack instead of out of a table, so there is no bounds check
// and no index. The type still has to match: a (ref null $t) can hold a
// reference the validator only knows as $t, and null has to trap.
d_m3Op(CallRef)
{
    IM3Function function = slot(IM3Function);
    IM3FuncType type = immediate(IM3FuncType);
    i32         stackOffset = immediate(i32);
    IM3Memory   memory = m3MemInfo(_mem);

    m3stack_t   sp = _sp + stackOffset;

    m3ret_t     r = m3Err_none;

    if (M3_LIKELY(function)) {
        if (M3_LIKELY(type == function->funcType)) {
            if (M3_UNLIKELY(not function->compiled)) {
                r = CompileFunction(function);
            }

            if (M3_LIKELY(not r)) {
#if (d_m3EnableOpProfiling || d_m3EnableOpTracing)
                r = Call(function->compiled, sp, _mem, d_m3OpDefaultArgs, d_m3BaseCstr);
#else
                r = Call(function->compiled, sp, _mem, d_m3OpDefaultArgs);
#endif

                _mem = memory->mallocated;

                if (M3_LIKELY(not r)) {
                    nextOpDirect();
                } else {
                    d_m3RecordCallFrame(memory, function);
                    pushBacktraceFrame();
                    forwardTrap(r);
                }
            }
        } else {
            r = m3Err_trapIndirectCallTypeMismatch;
        }
    } else {
        r = m3Err_trapNullFunctionRef;
    }

    if (M3_UNLIKELY(r)) {
        newTrap(r);
    } else {
        forwardTrap(r);
    }
}


// ref.as_non_null is a null check and nothing else: the value stays put, so
// there is no destination slot, only the trap.
d_m3Op(RefAsNonNull)
{
    IM3Function reference = slot(IM3Function);

    if (M3_UNLIKELY(not reference)) {
        newTrap(m3Err_trapNullReference);
    }

    nextOp();
}


d_m3Op(CallIndirect)
{
    d_m3CheckNativeStack();

    M3Table*    table = immediate(M3Table*);
    u64         tableIndex = d_m3WideOperand(table->isTable64);
    IM3FuncType type = immediate(IM3FuncType);
    i32         stackOffset = immediate(i32);
    IM3Memory   memory = m3MemInfo(_mem);

    m3stack_t   sp = _sp + stackOffset;

    m3ret_t     r = m3Err_none;

    if (M3_LIKELY(tableIndex < table->size)) {
        IM3Function function = (IM3Function)table->elements[tableIndex];

        if (M3_LIKELY(function)) {
            if (M3_LIKELY(type == function->funcType)) {
                if (M3_UNLIKELY(not function->compiled)) {
                    r = CompileFunction(function);
                }

                if (M3_LIKELY(not r)) {
                    // a table may hold functions from another module, and those
                    // run against their own module's memory
                    M3MemoryHeader* calleeMem = Module_MemoryHeader(function->module);

#if (d_m3EnableOpProfiling || d_m3EnableOpTracing)
                    r = Call(function->compiled, sp, calleeMem, d_m3OpDefaultArgs, d_m3BaseCstr);
#else
                    r = Call(function->compiled, sp, calleeMem, d_m3OpDefaultArgs);
#endif

                    _mem = memory->mallocated;

                    if (M3_LIKELY(not r)) {
                        nextOpDirect();
                    } else {
                        d_m3RecordCallFrame(memory, function);
                        pushBacktraceFrame();
                        forwardTrap(r);
                    }
                }
            } else {
                r = m3Err_trapIndirectCallTypeMismatch;
            }
        } else {
            r = m3Err_trapTableElementIsNull;
        }
    } else {
        r = m3Err_trapTableIndexOutOfRange;
    }

    if (M3_UNLIKELY(r)) {
        newTrap(r);
    } else {
        forwardTrap(r);
    }
}


// return_call / return_call_indirect: the callee takes over this function's stack frame
// instead of getting one of its own.  The spec guarantees the callee returns exactly what
// the enclosing function returns, so the two frames share their return slots and only the
// arguments have to be relocated: the compiler stages them above the caller's stack (they
// can still read the args/locals they're about to overwrite) and the op slides them down
// onto the frame base.  The jump is a tail call, so neither the m3 stack nor the native
// stack grows.  Tail recursion therefore runs in constant space, unless the return_call
// sits inside a loop or a try region: those hold a native frame that the tail call walks
// away from instead of unwinding, and d_m3CheckNativeStack bounds the pile they make.
#define d_m3TailCallArgs(RETURNSLOTS, STACKOFFSET, NUMARGSLOTS)                     \
   memmove ((void *) (_sp + (RETURNSLOTS)), (const void *) (_sp + (STACKOFFSET)),   \
            (size_t) (NUMARGSLOTS) * sizeof (m3slot_t))

d_m3Op(ReturnCall)
{
    pc_t    callPC = immediate(pc_t);
    i32     stackOffset = immediate(i32);
    i32     returnSlots = immediate(i32);
    u32     numArgSlots = immediate(u32);

    m3ret_t possible_trap = m3_Yield();
    if (M3_UNLIKELY(possible_trap)) {
        newTrap(possible_trap);
    }

    d_m3TailCallArgs(returnSlots, stackOffset, numArgSlots);

    jumpOpDirect(callPC);
}


d_m3Op(ReturnCallRef)
{
    IM3Function function = slot(IM3Function);
    IM3FuncType type = immediate(IM3FuncType);
    i32         stackOffset = immediate(i32);
    i32         returnSlots = immediate(i32);
    u32         numArgSlots = immediate(u32);

    m3ret_t     r = m3Err_none;

    if (M3_LIKELY(function)) {
        if (M3_LIKELY(type == function->funcType)) {
            if (M3_UNLIKELY(not function->compiled)) {
                r = CompileFunction(function);
            }

            if (M3_LIKELY(not r)) {
                r = m3_Yield();

                if (M3_LIKELY(not r)) {
                    d_m3TailCallArgs(returnSlots, stackOffset, numArgSlots);

                    jumpOpDirect(function->compiled);
                }
            }
        } else {
            r = m3Err_trapIndirectCallTypeMismatch;
        }
    } else {
        r = m3Err_trapNullFunctionRef;
    }

    newTrap(r);
}


d_m3Op(ReturnCallIndirect)
{
    M3Table*    table = immediate(M3Table*);
    u64         tableIndex = d_m3WideOperand(table->isTable64);
    IM3FuncType type = immediate(IM3FuncType);
    i32         stackOffset = immediate(i32);
    i32         returnSlots = immediate(i32);
    u32         numArgSlots = immediate(u32);

    m3ret_t     r = m3Err_none;

    if (M3_LIKELY(tableIndex < table->size)) {
        IM3Function function = (IM3Function)table->elements[tableIndex];

        if (M3_LIKELY(function)) {
            if (M3_LIKELY(type == function->funcType)) {
                if (M3_UNLIKELY(not function->compiled)) {
                    r = CompileFunction(function);
                }

                if (M3_LIKELY(not r)) {
                    r = m3_Yield();

                    if (M3_LIKELY(not r)) {
                        d_m3TailCallArgs(returnSlots, stackOffset, numArgSlots);

                        jumpOpDirect(function->compiled);
                    }
                }
            } else {
                r = m3Err_trapIndirectCallTypeMismatch;
            }
        } else {
            r = m3Err_trapTableElementIsNull;
        }
    } else {
        r = m3Err_trapTableIndexOutOfRange;
    }

    newTrap(r);
}


d_m3Op(CallRawFunction)
{
    d_m3TracePrepare

    M3ImportContext ctx;

    M3RawCall       call = (M3RawCall)(*_pc++);
    ctx.function = immediate(IM3Function);
    ctx.userdata = immediate(void*);
    u64* const sp = ((u64*)_sp);
    IM3Memory  memory = m3MemInfo(_mem);

    IM3Runtime runtime = m3MemRuntime(_mem);

#if d_m3EnableStrace
    IM3FuncType ftype = ctx.function->funcType;

    char        outbuff[1024];
    char*       outp = outbuff;
    char*       oute = outbuff + 1024;

    outp += snprintf(outp, oute - outp, "%s!%s(", ctx.function->import.moduleUtf8, ctx.function->import.fieldUtf8);

    const int nArgs = ftype->numArgs;
    const int nRets = ftype->numRets;
    u64*      args = sp + nRets;
    for (int i = 0; i < nArgs; i++) {
        const int type = ftype->types[nRets + i];
        switch (type) {
        case c_m3Type_i32: outp += snprintf(outp, oute - outp, "%" PRIi32, *(i32*)(args + i)); break;
        case c_m3Type_i64: outp += snprintf(outp, oute - outp, "%" PRIi64, *(i64*)(args + i)); break;
        case c_m3Type_f32: outp += snprintf(outp, oute - outp, "%" PRIf32, *(f32*)(args + i)); break;
        case c_m3Type_f64: outp += snprintf(outp, oute - outp, "%" PRIf64, *(f64*)(args + i)); break;
        default: outp += snprintf(outp, oute - outp, "<type %d>", type); break;
        }
        if (i < nArgs - 1) {
            outp += snprintf(outp, oute - outp, ", ");
        }
    }
    // closed here rather than after the last argument: an import taking none has
    // no last argument to close it
    outp += snprintf(outp, oute - outp, ")");
#  if d_m3EnableStrace >= 2
    outp += snprintf(outp, oute - outp, " { <native> }");
#  endif
#endif

    // m3_Call uses runtime->stack to set-up initial exported function stack.
    // Reconfigure the stack to enable recursive invocations of m3_Call.
    // I.e. exported/table function can be called from an impoted function.
    void* stack_backup = runtime->stack;
    runtime->stack = sp;
    // the memory this import was bound to, which is memory 0 unless the host
    // module named another one. _mem itself stays on the caller's memory:
    // the trap path below and the backtrace recorder still read it.
    m3ret_t possible_trap = call(runtime, &ctx, sp, m3MemData(ctx.function->hostMemory->mallocated));
    runtime->stack = stack_backup;

#if d_m3EnableStrace
    if (M3_UNLIKELY(possible_trap)) {
        d_m3TracePrint("%s -> %s", outbuff, (char*)possible_trap);
    } else if (ftype and ftype->numRets) {
        d_m3TracePrint("%s = %s", outbuff, SPrintFunctionRetList(ftype, (m3stack_t)sp));
    } else {
        d_m3TracePrint("%s", outbuff);
    }
#endif

    if (M3_UNLIKELY(possible_trap)) {
        _mem = memory->mallocated;
        pushBacktraceFrame();
    }
    forwardTrap(possible_trap);
}


d_m3Op(MemSize)
{
    IM3Memory memory = m3MemInfo(_mem);

    _r0 = memory->numPages;

    nextOp();
}


d_m3Op(MemGrow)
{
    IM3Runtime runtime = m3MemRuntime(_mem);
    IM3Memory  memory = m3MemInfo(_mem);

    i32        numPagesToGrow = (i32)_r0;
    if (numPagesToGrow >= 0) {
        _r0 = (m3reg_t)memory->numPages;

        if (M3_LIKELY(numPagesToGrow)) {
            u64      requiredPages = memory->numPages + (u32)numPagesToGrow;

            M3Result r = ResizeMemory(runtime, memory, requiredPages);
            if (r) {
                _r0 = -1;
            }

            _mem = memory->mallocated;
        }
    } else {
        _r0 = -1;
    }

    nextOp();
}


#if d_m3HasMemory64

// memory.grow on a 64-bit memory. The delta is a full u64 rather than a value
// the sign of which can stand in for "too big", and failure answers 2^64-1
// instead of 2^32-1 - which is the same -1 in the register, sized by the slot
// it is written to.
d_m3Op(MemGrow64)
{
    IM3Runtime runtime = m3MemRuntime(_mem);
    IM3Memory  memory = m3MemInfo(_mem);

    u64        numPagesToGrow = (u64)_r0;
    u64        numPages = memory->numPages;

    if (M3_LIKELY(numPagesToGrow)) {
        // maxPages is never below the current size, so the difference cannot
        // wrap; asking for more than it is a request nothing could satisfy
        if (numPagesToGrow <= memory->maxPages - numPages and
            not ResizeMemory(runtime, memory, numPages + numPagesToGrow)) {
            _r0 = (m3reg_t)numPages;
        } else {
            _r0 = -1;
        }

        _mem = memory->mallocated;
    } else {
        _r0 = (m3reg_t)numPages;
    }

    nextOp();
}

#endif // d_m3HasMemory64


d_m3Op(MemCopy)
{
    u64 size = (u32)_r0;
    u64 source = slot(u32);
    u64 destination = slot(u32);

    if (M3_LIKELY(d_m3MemRangeOk(destination, size, _mem))) {
        if (M3_LIKELY(d_m3MemRangeOk(source, size, _mem))) {
            u8* dst = m3MemData(_mem) + destination;
            u8* src = m3MemData(_mem) + source;
            memmove(dst, src, (size_t)size);

            nextOp();
        } else {
            d_outOfBoundsMemOp(source, size);
        }
    } else {
        d_outOfBoundsMemOp(destination, size);
    }
}


#if d_m3HasMemory64

// memory.copy within a single 64-bit memory: every operand is a full u64.
d_m3Op(MemCopy64)
{
    u64 size = (u64)_r0;
    u64 source = slot(u64);
    u64 destination = slot(u64);

    if (M3_LIKELY(d_m3MemRangeOk(destination, size, _mem))) {
        if (M3_LIKELY(d_m3MemRangeOk(source, size, _mem))) {
            u8* dst = m3MemData(_mem) + destination;
            u8* src = m3MemData(_mem) + source;
            memmove(dst, src, (size_t)size);

            nextOp();
        } else {
            d_outOfBoundsMemOp(source, size);
        }
    } else {
        d_outOfBoundsMemOp(destination, size);
    }
}

#endif // d_m3HasMemory64


// Points the _mem register at another memory: one of the module's own, or
// memory 0 of a module being called into. The compiler emits these in pairs,
// around a single access or a single cross-module call, so _mem is back where
// it belongs before anything that reads it for another purpose - a backtrace,
// the stack-limit check - can run.
d_m3Op(SetMemory)
{
    IM3Memory memory = immediate(IM3Memory);

    _mem = memory->mallocated;

    nextOp();
}


#if d_m3HasMultiMemory

// memory.copy between two different memories. The pair-of-SetMemory trick only
// reaches one memory at a time, so this one names both outright.
//
// The two need not be addressed the same way, so each operand's width comes
// from the memory it belongs to: bit 0 of the widths immediate for the
// destination, bit 1 for the source. The length follows the narrower of the
// two, which is 64-bit only when both are.
d_m3Op(MemCopy_x)
{
    IM3Memory       destMemory = immediate(IM3Memory);
    IM3Memory       sourceMemory = immediate(IM3Memory);
    u32             widths = immediate(u32);

    M3MemoryHeader* destMem = destMemory->mallocated;
    M3MemoryHeader* sourceMem = sourceMemory->mallocated;

    u64             size = (widths == 0x3) ? (u64)_r0 : (u64)(u32)_r0;
    u64             source = d_m3WideOperand(widths & 0x2);
    u64             destination = d_m3WideOperand(widths & 0x1);

    if (M3_LIKELY(d_m3MemRangeOk(destination, size, destMem))) {
        if (M3_LIKELY(d_m3MemRangeOk(source, size, sourceMem))) {
            memmove(m3MemData(destMem) + destination, m3MemData(sourceMem) + source, (size_t)size);

            nextOp();
        } else {
            d_outOfBoundsMemOp(source, size);
        }
    } else {
        d_outOfBoundsMemOp(destination, size);
    }
}

#endif // d_m3HasMultiMemory


d_m3Op(MemFill)
{
    u32 size = (u32)_r0;
    u32 byte = slot(u32);
    u64 destination = slot(u32);

    if (M3_LIKELY(d_m3MemRangeOk(destination, size, _mem))) {
        u8* mem8 = m3MemData(_mem) + destination;
        memset(mem8, (u8)byte, size);
        nextOp();
    } else {
        d_outOfBoundsMemOp(destination, size);
    }
}


#if d_m3HasMemory64

// memory.fill on a 64-bit memory. The byte to write stays an i32 - it is a
// value, not an address - so only the destination and the length widen.
d_m3Op(MemFill64)
{
    u64 size = (u64)_r0;
    u32 byte = slot(u32);
    u64 destination = slot(u64);

    if (M3_LIKELY(d_m3MemRangeOk(destination, size, _mem))) {
        u8* mem8 = m3MemData(_mem) + destination;
        memset(mem8, (u8)byte, (size_t)size);
        nextOp();
    } else {
        d_outOfBoundsMemOp(destination, size);
    }
}

#endif // d_m3HasMemory64


d_m3Op(MemInit)
{
    M3DataSegment* segment = immediate(M3DataSegment*);

    u64            size = (u32)_r0;
    u64            source = slot(u32);
    u64            destination = slot(u32);

    u64            available = segment->dropped ? 0 : segment->size;

    if (M3_LIKELY(d_m3MemRangeOk(destination, size, _mem))) {
        if (M3_LIKELY(source <= available and size <= available - source)) {
            memcpy(m3MemData(_mem) + destination, segment->data + source, (size_t)size);
            nextOp();
        } else {
            d_outOfBoundsMemOp(source, size);
        }
    } else {
        d_outOfBoundsMemOp(destination, size);
    }
}


#if d_m3HasMemory64

// memory.init into a 64-bit memory. The segment is indexed as an i32 whatever
// the memory is, so only the destination widens.
d_m3Op(MemInit64)
{
    M3DataSegment* segment = immediate(M3DataSegment*);

    u64            size = (u32)_r0;
    u64            source = slot(u32);
    u64            destination = slot(u64);

    u64            available = segment->dropped ? 0 : segment->size;

    if (M3_LIKELY(d_m3MemRangeOk(destination, size, _mem))) {
        if (M3_LIKELY(source <= available and size <= available - source)) {
            memcpy(m3MemData(_mem) + destination, segment->data + source, (size_t)size);
            nextOp();
        } else {
            d_outOfBoundsMemOp(source, size);
        }
    } else {
        d_outOfBoundsMemOp(destination, size);
    }
}

#endif // d_m3HasMemory64


d_m3Op(DataDrop)
{
    M3DataSegment* segment = immediate(M3DataSegment*);
    segment->dropped = true;

    nextOp();
}


#if d_m3HasRefTypes

d_m3Op(TableGet)
{
    M3Table* table = immediate(M3Table*);
    u64      index = d_m3WideOperand(table->isTable64);

    if (M3_LIKELY(index < table->size)) {
        _r0 = (u64)(uintptr_t)table->elements[index];
        nextOp();
    } else {
        newTrap(m3Err_trapTableOutOfBounds);
    }
}


d_m3Op(TableSet)
{
    M3Table* table = immediate(M3Table*);
    void*    value = slot(void*);
    u64      index = d_m3WideOperand(table->isTable64);

    if (M3_LIKELY(index < table->size)) {
        table->elements[index] = value;
        nextOp();
    } else {
        newTrap(m3Err_trapTableOutOfBounds);
    }
}


d_m3Op(TableSize)
{
    M3Table* table = immediate(M3Table*);

    _r0 = table->size;

    nextOp();
}


d_m3Op(TableGrow)
{
    M3Table* table = immediate(M3Table*);
    u64      delta = d_m3WideOperand(table->isTable64);
    void*    value = slot(void*);

    u32      oldSize = table->size;
    u32      maxSize = table->maxSize ? table->maxSize : d_m3MaxSaneTableSize;

    // a table64 delta is a whole u64, so the sum is formed only once it is
    // known to fit
    u64      newSize = (delta <= maxSize - oldSize) ? (u64)oldSize + delta
                                                    : (u64)maxSize + 1;

    if (newSize == oldSize)         // a zero delta never reallocates, and the table may have no storage yet
    {
        _r0 = oldSize;
        nextOp();
    }

    if (newSize <= maxSize) {
        void** elements = m3_ReallocArray(void*, table->elements, (size_t)newSize, oldSize);

        if (elements) {
            table->elements = elements;
            table->size = (u32)newSize;

            for (u32 i = oldSize; i < table->size; ++i) {
                table->elements[i] = value;
            }

            _r0 = oldSize;
            nextOp();
        }
    }

    // spec: a failed grow returns -1 - which for a table64 is 2^64-1, and the
    // slot it is pushed to is what decides how much of the register is seen
    _r0 = -1;

    nextOp();
}


d_m3Op(TableInit)
{
    M3Table*          table = immediate(M3Table*);
    M3ElementSegment* segment = immediate(M3ElementSegment*);
    u32               count = slot(u32);
    u32               source = slot(u32);
    // an element segment is indexed as an i32 whatever the table is; only the
    // destination follows the table's index type
    u64               destination = d_m3WideOperand(table->isTable64);

    u64               available = segment->dropped ? 0 : segment->numElements;

    if (M3_LIKELY(destination <= table->size and count <= table->size - destination and
                  (u64) source + count <= available)) {
        for (u32 i = 0; i < count; ++i) {
            table->elements[destination + i] = segment->resolved[source + i];
        }

        nextOp();
    } else {
        newTrap(m3Err_trapTableOutOfBounds);
    }
}


d_m3Op(ElemDrop)
{
    M3ElementSegment* segment = immediate(M3ElementSegment*);
    segment->dropped = true;

    nextOp();
}


d_m3Op(TableCopy)
{
    M3Table* dst = immediate(M3Table*);
    M3Table* src = immediate(M3Table*);
    // the length follows the narrower of the two tables, so it is 64-bit only
    // when both are
    u64      count = d_m3WideOperand(dst->isTable64 and src->isTable64);
    u64      source = d_m3WideOperand(src->isTable64);
    u64      destination = d_m3WideOperand(dst->isTable64);

    if (M3_LIKELY(destination <= dst->size and count <= dst->size - destination and
                  source <= src->size and count <= src->size - source)) {
        memmove(dst->elements + destination, src->elements + source, (size_t)count * sizeof(void*));

        nextOp();
    } else {
        newTrap(m3Err_trapTableOutOfBounds);
    }
}


d_m3Op(TableFill)
{
    M3Table* table = immediate(M3Table*);
    u64      count = d_m3WideOperand(table->isTable64);
    void*    value = slot(void*);
    u64      index = d_m3WideOperand(table->isTable64);

    if (M3_LIKELY(index <= table->size and count <= table->size - index)) {
        for (u64 i = 0; i < count; ++i) {
            table->elements[index + i] = value;
        }

        nextOp();
    } else {
        newTrap(m3Err_trapTableOutOfBounds);
    }
}

#endif // d_m3HasRefTypes


// it's a debate: should the compilation be trigger be the caller or callee page.
// it's a much easier to put it in the caller pager. if it's in the callee, either the entire page
// has be left dangling or it's just a stub that jumps to a newly acquired page.  In Gestalt, I opted
// for the stub approach. Stubbing makes it easier to dynamically free the compilation. You can also
// do both.
d_m3Op(Compile)
{
    rewrite_op(op_Call);

    IM3Function function = immediate(IM3Function);

    m3ret_t     result = m3Err_none;

    if (M3_UNLIKELY(not function->compiled)) { // check to see if function was compiled since this operation was emitted.
        result = CompileFunction(function);
    }

    if (not result) {
        // patch up compiled pc and call rewritten op_Call
        *((void**)--_pc) = (void*)(function->compiled);
        --_pc;
        nextOpDirect();
    }

    newTrap(result);
}


// same as op_Compile, for a call site that was emitted as a tail call
d_m3Op(CompileReturnCall)
{
    rewrite_op(op_ReturnCall);

    IM3Function function = immediate(IM3Function);

    m3ret_t     result = m3Err_none;

    if (M3_UNLIKELY(not function->compiled)) {
        result = CompileFunction(function);
    }

    if (not result) {
        // patch up compiled pc and call rewritten op_ReturnCall
        *((void**)--_pc) = (void*)(function->compiled);
        --_pc;
        nextOpDirect();
    }

    newTrap(result);
}


#if d_m3HasStackSwitching
// Suspending for a reason that is not a control tag - the host asked, or the
// gas ran out. There is no handler to look for and no payload to carry, and
// clearing the routing a previous suspend left behind is what keeps this from
// being mistaken for one: a resume reads those fields to decide where to send
// the marker, and would otherwise send this one into whatever stub was last
// used, with whatever payload last went with it.
//
// No continuation handles it either. A resume that finds itself named as the
// handler takes the suspend as its own, so naming the suspending continuation
// would stop a suspend from inside a resumed one at that resume, with no stub
// to run. Naming none sends it through every resume up to the host.
static
void SuspendWithoutTag (IM3Runtime io_runtime, IM3Continuation io_cont, pc_t i_pc, m3stack_t i_sp,
                        m3reg_t i_r0
#  if d_m3HasFloat
                        ,
                        f64 i_fp0
#  endif
)
{
    io_runtime->suspendRequested = false;

    io_runtime->suspendTag = NULL;
    io_runtime->suspendHandlerCont = NULL;
    io_runtime->suspendStubPC = NULL;
    io_runtime->switchTarget = NULL;
    io_runtime->numSuspendPayload = 0;

    io_cont->pc = i_pc;
    io_cont->sp = i_sp;
    io_cont->r0 = i_r0;
#  if d_m3HasFloat
    io_cont->fp0 = i_fp0;
#  endif

    io_cont->numSuspendResults = 0;
}
#endif // d_m3HasStackSwitching


#if d_m3HasGasMetering

// Pays for the straight-line segment of the function body that follows, before
// any of it runs. The compiler adds up the segment's instruction costs while it
// compiles them and leaves the total here as an immediate, so metering costs one
// subtract and one branch however long the segment is.
//
// The counter is allowed to go negative - the segment was charged in full - and
// nothing resets it, so the trap stands until the host arms the runtime again.
d_m3Op(UseGas)
{
    IM3Runtime runtime = m3MemRuntime(_mem);
    u32        cost = immediate(u32);

    runtime->gasRemaining -= (i64)cost;

    if (M3_UNLIKELY(runtime->gasRemaining < 0)) {
#  if d_m3HasStackSwitching
        if (runtime->isSuspendable and runtime->activeContinuation) {
            // the operation has not run, so it has not spent anything either
            runtime->gasRemaining += (i64)cost;

            SuspendWithoutTag(runtime, runtime->activeContinuation, _pc - 2, _sp, d_m3ExpRegArgs(_r0, _fp0));

            return m3Err_continuationSuspended;
        }
#  endif
        newTrap(m3Err_trapOutOfGas);
    }

    nextOp();
}

#endif // d_m3HasGasMetering


d_m3Op(Entry)
{
    d_m3ClearRegisters

    d_m3TracePrepare

    IM3Function function = immediate(IM3Function);
#if d_m3EntryKeepsFrame
    IM3Memory memory = m3MemInfo(_mem);
#endif

#if d_m3SkipStackCheck
    if (true)
#elif d_m3HasStackSwitching
    IM3Continuation _act = (_mem ? m3MemRuntime(_mem)->activeContinuation : NULL);
    void*           maxStack = (_act && _act->valStack)
                                 ? (void*)(_act->valStack + _act->numStackSlots)
                                 : (_mem ? _mem->maxStack : (void*)~(uintptr_t)0);
    if (M3_LIKELY((void*)(_sp + function->maxStackSlots) < maxStack))
#else
    if (M3_LIKELY((void*)(_sp + function->maxStackSlots) < _mem->maxStack))
#endif
    {
#if defined(DEBUG)
        function->hits++;
#endif
        u8* stack = (u8*)((m3slot_t*)_sp + function->numRetAndArgSlots);

        memset(stack, 0x0, function->numLocalBytes);
        stack += function->numLocalBytes;

        if (function->constants) {
            memcpy(stack, function->constants, function->numConstantBytes);
        }

#if !d_m3EntryKeepsFrame
        // there's nothing left to do once the body returns, so hand this native frame
        // over to it. That's what lets op_ReturnCall tail-call in constant native stack.
        nextOpDirect();
#else

#  if d_m3EnableStrace >= 2
        d_m3TracePrint("%s %s {", m3_GetFunctionName(function), SPrintFunctionArgList(function, _sp + function->numRetSlots));
        trace_rt->callDepth++;
#  endif

        m3ret_t r = nextOpImpl();

#  if d_m3EnableStrace >= 2
        trace_rt->callDepth--;

        if (r) {
            d_m3TracePrint("} !trap = %s", (char*)r);
        } else if (GetFunctionNumReturns(function)) {
            d_m3TracePrint("} = %s", SPrintFunctionRetList(function->funcType, _sp));
        } else {
            d_m3TracePrint("}");
        }
#  endif

        if (M3_UNLIKELY(r)) {
            _mem = memory->mallocated;
#  if d_m3HasStackSwitching
            if (M3_UNLIKELY(r == m3Err_continuationSuspended)) {
                return RecordEntryFrame(m3MemRuntime(_mem), _pc, _sp, memory, function);
            }
#  endif
            fillBacktraceFrame();
        }
        forwardTrap(r);
#endif // d_m3EntryKeepsFrame
    } else {
        newTrap(m3Err_trapStackOverflow);
    }
}


d_m3Op(Loop)
{
    d_m3CheckNativeStack();

    d_m3TracePrepare

    // regs are unused coming into a loop anyway
    // this reduces code size & stack usage
    d_m3ClearRegisters

    m3ret_t   r;

    IM3Memory memory = m3MemInfo(_mem);

    do {
#if d_m3EnableStrace >= 3
        d_m3TracePrint("iter {");
        trace_rt->callDepth++;
#endif
        r = nextOpImpl();

#if d_m3EnableStrace >= 3
        trace_rt->callDepth--;
        d_m3TracePrint("}");
#endif
        // linear memory pointer needs refreshed here because the block it's looping over
        // can potentially invoke the grow operation.
        _mem = memory->mallocated;
    } while (r == _pc);

#if d_m3HasStackSwitching
    // The loop's identity is its own pc, which is also where its body starts,
    // so replaying it needs nothing else. Without this the continue sentinel
    // has no frame left to land in after a resume and gets mistaken for a trap.
    if (M3_UNLIKELY(r == m3Err_continuationSuspended)) {
        return RecordLoopFrame(m3MemRuntime(_mem), _pc, _sp, memory);
    }
#endif

    forwardTrap(r);
}


#if d_m3HasExceptionHandling

// try_table. Like op_Loop this claims a native frame, and for the same reason:
// a throw unwinds by returning m3Err_pendingException up the native stack, so
// something has to be standing there to catch it.
//
// Immediates: the clause count, then two words per clause - the tag to match
// (NULL for catch_all / catch_all_ref) and the PC of the stub that moves the
// payload into place and branches to the clause's label. The body follows.
//
// The frame stays alive after the body falls through or branches out of the try
// region, but the handler record is popped at those points (op_PopHandlers), so
// a later throw sees this frame is no longer the innermost one and passes it by.
//
// The handler stack is just a depth counter: an unwind stops at whichever frame
// finds the count still standing at the value its own push produced, which is
// the innermost try region that hasn't been popped.
d_m3Op(TryTable)
{
    d_m3CheckNativeStack();

    IM3Memory  memory = m3MemInfo(_mem);
    IM3Runtime runtime = m3MemRuntime(_mem);

    u32        numClauses = immediate(u32);

    pc_t       clauses = _pc;
    pc_t       body = _pc + 2 * numClauses;

    u32        depth = ++runtime->tryDepth;

    m3ret_t    r = jumpOpImpl(body);

    // the body can have grown linear memory out from under us
    _mem = memory->mallocated;

    // control is leaving this frame for good, so this try is done either way
    bool isInnermost = (runtime->tryDepth == depth);
    runtime->tryDepth = depth - 1;

#  if d_m3HasStackSwitching
    if (M3_UNLIKELY(r == m3Err_continuationSuspended)) {
        return RecordTryFrame(runtime, clauses, _sp, memory, numClauses, isInnermost);
    }
#  endif

    if (M3_UNLIKELY(r == m3Err_pendingException) and isInnermost) {
        const M3Exception* exception = runtime->pendingException;

        for (u32 i = 0; i < numClauses; ++i) {
            IM3Tag tag = (IM3Tag)clauses[2 * i];

            if (tag == NULL or tag == exception->tag) {
                pc_t handler = (pc_t)clauses[2 * i + 1];

                // an unwind leaves nothing meaningful in the registers
                d_m3ClearRegisters

                jumpOpDirect(handler);
            }
        }

        // no clause matched: the exception carries on up
    }

    forwardTrap(r);
}


// Head of a catch clause's stub: copies the caught exception's payload into the
// slots the stub's branch expects, and the exnref itself for a _ref clause.
//
// A clause that takes no exnref is the end of the road for this exception, so
// it is released here rather than waiting for the call to finish - a module
// that throws inside a loop would otherwise pile up one object per lap. Not so
// once an exnref has been handed out: a local, a global or a table may still be
// holding it, ready for throw_ref.
d_m3Op(CatchPayload)
{
    IM3Runtime   runtime = m3MemRuntime(_mem);
    M3Exception* exception = runtime->pendingException;

    u32          numArgs = immediate(u32);

    for (u32 i = 0; i < numArgs; ++i) {
        i32 offset = immediate(i32);
        u32 is64 = immediate(u32);
        u64 value = exception->args[i];

        if (is64) {
            *(u64*)(_sp + offset) = value;
        } else {
            *(u32*)(_sp + offset) = (u32)value;
        }
    }

    i32 refOffset = immediate(i32);

    if (refOffset >= 0) {
        *(M3Exception**)(_sp + refOffset) = exception;
        exception->reified = true;
    } else if (not exception->reified) {
        FreeException(runtime, exception);
    }

    nextOp();
}


d_m3Op(Throw)
{
    IM3Runtime   runtime = m3MemRuntime(_mem);

    IM3Tag       tag = immediate(IM3Tag);
    u32          numArgs = immediate(u32);

    M3Exception* exception = NewException(runtime, tag, numArgs);

    if (M3_UNLIKELY(not exception)) {
        newTrap(m3Err_mallocFailed);
    }

    for (u32 i = 0; i < numArgs; ++i) {
        i32 offset = immediate(i32);
        u32 is64 = immediate(u32);

        exception->args[i] = is64 ? *(u64*)(_sp + offset) : *(u32*)(_sp + offset);
    }

    runtime->pendingException = exception;

    return m3Err_pendingException;
}


d_m3Op(ThrowRef)
{
    M3Exception* exception = slot(M3Exception*);

    if (M3_UNLIKELY(not exception)) {
        newTrap(m3Err_trapNullReference);
    }

    m3MemRuntime(_mem)->pendingException = exception;

    return m3Err_pendingException;
}


// Emitted where control leaves a try region without leaving op_TryTable's native
// frame: falling out of the block's end, or branching past it. The count is how
// many try regions the exit crosses.
d_m3Op(PopHandlers)
{
    IM3Runtime runtime = m3MemRuntime(_mem);

    u32        count = immediate(u32);

    runtime->tryDepth -= count;

    nextOp();
}

#endif // d_m3HasExceptionHandling

#if d_m3HasStackSwitching

d_m3Op(ContNew)
{
    IM3Runtime  runtime = m3MemRuntime(_mem);
    IM3FuncType funcType = immediate(IM3FuncType);
    i32         dstSlot = immediate(i32);
    i32         funcSlot = immediate(i32);

    IM3Function func = *(IM3Function*)(_sp + funcSlot);
    if (M3_UNLIKELY(not func)) {
        newTrap(m3Err_trapNullFunctionRef);
    }

    IM3Continuation cont = Continuation_New(runtime, funcType, func);
    if (M3_UNLIKELY(not cont)) {
        newTrap(m3Err_mallocFailed);
    }

    *(IM3Continuation*)(_sp + dstSlot) = cont;
    nextOp();
}


static
IM3Continuation InnermostSuspended (IM3Continuation i_cont);

d_m3Op(ContBind)
{
    IM3Runtime      runtime = m3MemRuntime(_mem);
    IM3FuncType     ct1 = immediate(IM3FuncType);
    IM3FuncType     ct2 = immediate(IM3FuncType);
    i32             dstSlot = immediate(i32);
    i32             contSlot = immediate(i32);
    u32             numBound = immediate(u32);

    IM3Continuation srcCont = *(IM3Continuation*)(_sp + contSlot);
    if (M3_UNLIKELY(not srcCont)) {
        newTrap(m3Err_trapNullContinuationRef);
    }
    if (M3_UNLIKELY(srcCont->state == cont_consumed || srcCont->state == cont_running)) {
        newTrap(m3Err_trapContinuationConsumed);
    }

    IM3Continuation dstCont = Continuation_New(runtime, ct2, srcCont->entryFunction);
    if (M3_UNLIKELY(not dstCont)) {
        newTrap(m3Err_mallocFailed);
    }

    // the bound continuation takes over the original's execution state whole,
    // recorded native frames included: binding a suspended continuation must
    // not cost it the frames it has to be resumed through
    m3_Free(dstCont->valStack);
    m3_Free(dstCont->frames);
    dstCont->entryFunction = srcCont->entryFunction;
    dstCont->valStack = srcCont->valStack;
    dstCont->numStackSlots = srcCont->numStackSlots;
    dstCont->frames = srcCont->frames;
    dstCont->framesCap = srcCont->framesCap;
    dstCont->numFrames = srcCont->numFrames;
    dstCont->sp = srcCont->sp;
    dstCont->pc = srcCont->pc;
    dstCont->r0 = srcCont->r0;
#  if d_m3HasFloat
    dstCont->fp0 = srcCont->fp0;
#  endif
    dstCont->boundArgsCount = srcCont->boundArgsCount;
    dstCont->state = srcCont->state;

    dstCont->numSuspendResults = srcCont->numSuspendResults;
    for (u32 j = 0; j < srcCont->numSuspendResults; ++j) {
        dstCont->suspendResultOffsets[j] = srcCont->suspendResultOffsets[j];
        dstCont->suspendResultIs64[j] = srcCont->suspendResultIs64[j];
    }

    if (dstCont->state == cont_allocated) {
        u16 numRets = ct1->contFuncType ? GetFuncTypeNumResults(ct1->contFuncType) : 0;
        for (u32 i = 0; i < numBound; ++i) {
            i32 argSlot = immediate(i32);
            u32 is64 = immediate(u32);
            u32 targetIdx = (numRets + dstCont->boundArgsCount) * c_ioSlotCount;
            if (is64) {
                *(u64*)(dstCont->valStack + targetIdx) = *(u64*)(_sp + argSlot);
            } else {
                *(u32*)(dstCont->valStack + targetIdx) = *(u32*)(_sp + argSlot);
            }
            dstCont->boundArgsCount++;
        }
    } else {
        IM3Continuation inner = InnermostSuspended(dstCont);
        for (u32 i = 0; i < numBound; ++i) {
            i32 argSlot = immediate(i32);
            u32 is64 = immediate(u32);
            u32 dstIdx = dstCont->boundArgsCount;
            if (dstIdx < inner->numSuspendResults) {
                i32 dstOffset = inner->suspendResultOffsets[dstIdx];
                if (is64) {
                    *(u64*)(inner->sp + dstOffset) = *(u64*)(_sp + argSlot);
                } else {
                    *(u32*)(inner->sp + dstOffset) = *(u32*)(_sp + argSlot);
                }
            }
            dstCont->boundArgsCount++;
        }
    }

    srcCont->valStack = NULL;
    srcCont->frames = NULL;
    srcCont->framesCap = 0;
    srcCont->numFrames = 0;
    srcCont->state = cont_consumed;

    *(IM3Continuation*)(_sp + dstSlot) = dstCont;
    nextOp();
}


// Finds the resume whose handler answers this tag, walking out through the
// continuations that resumed us. i_kind picks which sort of handler counts -
// 0 for (on $e $l), 1 for (on $e switch). The spec is explicit that the search
// walks past a handler of the wrong sort even when the tag matches, so the
// kind is part of the match rather than something checked afterwards.
static
IM3Continuation FindHandler (IM3Continuation i_cont, IM3Tag i_tag, u8 i_kind, pc_t* o_stubPC)
{
    IM3Tag queryTag = (i_tag and i_tag->resolved) ? i_tag->resolved : i_tag;

    for (IM3Continuation cont = i_cont; cont; cont = cont->parent) {
        for (u32 h = 0; h < cont->numHandlers; ++h) {
            pc_t   entry = cont->handlersPC + (h * 3);
            IM3Tag handlerTag = (IM3Tag)(*(entry + 1));
            if (handlerTag and handlerTag->resolved) {
                handlerTag = handlerTag->resolved;
            }

            if ((u32)(uintptr_t)(*entry) == i_kind and handlerTag == queryTag) {
                if (o_stubPC) {
                    *o_stubPC = (pc_t)(*(entry + 2));
                }
                return cont;
            }
        }
    }

    return NULL;
}


d_m3Op(Suspend)
{
    IM3Runtime      runtime = m3MemRuntime(_mem);
    IM3Continuation cont = runtime->activeContinuation;
    if (M3_UNLIKELY(not cont)) {
        newTrap(m3Err_trapUnhandledControlTag);
    }

    IM3Tag tag = immediate(IM3Tag);
    u32    numParams = immediate(u32);

    runtime->numSuspendPayload = numParams;
    for (u32 i = 0; i < numParams; ++i) {
        i32 paramSlot = immediate(i32);
        u32 is64 = immediate(u32);
        if (is64) {
            runtime->suspendPayload[i] = *(u64*)(_sp + paramSlot);
        } else {
            runtime->suspendPayload[i] = (u64) * (u32*)(_sp + paramSlot);
        }
        runtime->suspendPayloadIs64[i] = is64;
    }

    u32 numResults = immediate(u32);
    cont->numSuspendResults = numResults;
    for (u32 i = 0; i < numResults; ++i) {
        cont->suspendResultOffsets[i] = immediate(i32);
        cont->suspendResultIs64[i] = immediate(u32);
    }

    pc_t            matchedStubPC = NULL;
    IM3Continuation searchCont = FindHandler(cont, tag, 0, &matchedStubPC);

    if (M3_UNLIKELY(not searchCont)) {
        newTrap(m3Err_trapUnhandledControlTag);
    }

    runtime->suspendStubPC = matchedStubPC;
    runtime->suspendHandlerCont = searchCont;
    runtime->switchTarget = NULL;
    runtime->suspendTag = tag;

    cont->pc = _pc;
    cont->sp = _sp;
    cont->r0 = _r0;
#  if d_m3HasFloat
    cont->fp0 = _fp0;
#  endif
    cont->boundArgsCount = 0;

    return m3Err_continuationSuspended;
}


// ReplayFrames and RunUnderHandlers call each other: a captured resume is a
// frame like any other, and what it stands back up is a continuation with
// frames of its own.
static
m3ret_t RunUnderHandlers (IM3Runtime i_runtime, IM3Continuation* io_cont,
                          pc_t i_handlersPC, u32 i_numHandlers, M3MemoryHeader** io_mem);
static
void TakeContinuationResults (IM3Continuation i_cont, m3stack_t i_sp,
                              pc_t i_resultsPC, u32 i_numResults);


// Rebuilds the native frames the suspend unwound through and lands on the
// suspension point at the bottom of them.
//
// Frames were recorded innermost first, which is the order the unwind met
// them, so this walks down from the outermost and recurses toward the
// suspension point: by the time anything runs again, the native stack looks
// the way it did when the suspend left.
//
// Each case has to stand in for one operation completely - claim the frame it
// claimed, and answer the return value the way it would - including recording
// itself again, because the continuation is free to suspend a second time from
// inside the chain being rebuilt here. That second suspend refills frames[]
// from index 0 while this walk is still in it, which is why every level takes
// its own copy of the frame on the way down, before anything can run.
static
m3ret_t ReplayFrames (IM3Continuation i_cont, i32 i_depth, M3MemoryHeader* _mem)
{
    IM3Runtime runtime = m3MemRuntime(_mem);
    m3ret_t    r = m3Err_none;

#  if d_m3MaxNativeStack > 0
    {
        void* limit = runtime->stackLimit;

        if (M3_UNLIKELY(limit and m3_NativeStackPtr() < limit)) {
            return m3Err_trapStackOverflow;
        }
    }
#  endif

    if (i_depth < 0) {
#  if d_m3HasExceptionHandling
        // resume_throw stops here: rather than carrying on from the suspension
        // point, the exception is raised at it and travels back out through the
        // try regions this walk has just rebuilt above
        if (M3_UNLIKELY(i_cont->resumeThrow)) {
            runtime->pendingException = i_cont->resumeThrow;
            i_cont->resumeThrow = NULL;

            return m3Err_pendingException;
        }
#  endif
        return d_m3CallWithRegs(i_cont->pc, i_cont->sp, _mem, i_cont->r0, i_cont->fp0);
    }

    M3Frame frame = i_cont->frames[i_depth];

    // frame_try brackets the recursion with its own bookkeeping, and
    // frame_resume descends into another continuation entirely, so neither
    // shares this one
    bool    ownDescent = ((M3FrameKind)frame.kind == frame_resume);

#  if d_m3HasExceptionHandling
    ownDescent = ownDescent or ((M3FrameKind)frame.kind == frame_try);
#  endif

    if (not ownDescent) {
        r = ReplayFrames(i_cont, i_depth - 1, _mem);
        _mem = frame.memory->mallocated;
    }

    switch ((M3FrameKind)frame.kind) {
    case frame_call:
        if (M3_LIKELY(not r)) {
            // the callee returned: carry on in the caller, with the registers
            // op_Call's own frame had been holding across the call
            return d_m3CallWithRegs(frame.pc, frame.sp, _mem, frame.call.r0, frame.call.fp0);
        }

        if (M3_UNLIKELY(r == m3Err_continuationSuspended)) {
            return Continuation_RecordFrame(runtime, &frame);
        }
#  if d_m3RecordBacktraces
        PushBacktraceFrame(runtime, frame.pc - 1);
#  endif
        return r;

    case frame_loop: {
        m3stack_t _sp = frame.sp;
        m3reg_t   _r0 = 0;
#  if d_m3HasFloat
        f64 _fp0 = 0.;
#  endif
        // op_Loop's protocol: the body hands its own pc back to ask for
        // another lap. Dispatched the way op_Loop dispatches it, so a yield
        // hook sees the same back edges it always did.
        while (r == (m3ret_t)frame.pc) {
            r = jumpOpImpl(frame.pc);
            _mem = frame.memory->mallocated;
        }

        if (M3_UNLIKELY(r == m3Err_continuationSuspended)) {
            return Continuation_RecordFrame(runtime, &frame);
        }
        return r;
    }

#  if d_m3HasExceptionHandling
    case frame_try: {
        m3stack_t _sp = frame.sp;
        m3reg_t   _r0 = 0;
#    if d_m3HasFloat
        f64 _fp0 = 0.;
#    endif
        (void)_sp;
        (void)_r0;
#    if d_m3HasFloat
        (void)_fp0;
#    endif

        // this try region is standing again, so it counts towards the depth
        // once more - unless op_PopHandlers had already retired it before the
        // suspend, in which case it is only here to be walked past
        u32 depth = ++runtime->tryDepth;

        if (not frame.try_.handlersLive) {
            runtime->tryDepth = depth - 1;
        }

        r = ReplayFrames(i_cont, i_depth - 1, _mem);
        _mem = frame.memory->mallocated;

        bool isInnermost = (runtime->tryDepth == depth);
        runtime->tryDepth = depth - 1;

        if (M3_UNLIKELY(r == m3Err_continuationSuspended)) {
            frame.try_.handlersLive = isInnermost;
            return Continuation_RecordFrame(runtime, &frame);
        }

        if (M3_UNLIKELY(r == m3Err_pendingException) and isInnermost) {
            const M3Exception* exception = runtime->pendingException;

            for (u32 i = 0; i < frame.try_.numClauses; ++i) {
                IM3Tag tag = (IM3Tag)frame.pc[2 * i];

                if (tag == NULL or tag == exception->tag) {
                    pc_t handler = (pc_t)frame.pc[2 * i + 1];

                    d_m3ClearRegisters

                    // op_TryTable hands its frame to the handler, but this is
                    // not an operation and cannot: the replay stands in for
                    // that frame, so it keeps it and passes the answer on.
                    return jumpOpImpl(handler);
                }
            }
        }
        return r;
    }
#  endif

    case frame_resume: {
        // A resume that the suspend passed straight through. Standing it up
        // again means running the continuation it had under it, under the same
        // handlers - and that continuation carries the suspension point, so it
        // is where the rest of the replay happens.
        IM3Continuation inner = frame.resume.cont;

        r = RunUnderHandlers(runtime, &inner, frame.resume.handlersPC,
                             frame.resume.numHandlers, &_mem);

        if (r == m3Err_none) {
            inner->state = cont_consumed;

            TakeContinuationResults(inner, frame.sp, frame.resume.resultsPC,
                                    frame.resume.numResults);

            return d_m3CallWithRegs(frame.pc, frame.sp, _mem, 0, 0.);
        }

        if (M3_UNLIKELY(r == m3Err_continuationSuspended)) {
            if (runtime->suspendHandlerCont != inner) {
                frame.resume.cont = inner;
                return Continuation_RecordFrame(runtime, &frame);
            }

            IM3Continuation suspended = Continuation_ForkSuspended(runtime, inner);
            if (M3_UNLIKELY(not suspended)) {
                return m3Err_mallocFailed;
            }
            runtime->suspendedContinuation = suspended;
            if (runtime->suspendStubPC) {
                return d_m3CallWithRegs(runtime->suspendStubPC, frame.sp, _mem, 0, 0.);
            } else {
                return m3Err_trapUnhandledControlTag;
            }
        } else {
            inner->state = cont_consumed;
        }

        return r;
    }

#  if d_m3EntryKeepsFrame
    case frame_entry:
        if (M3_UNLIKELY(r)) {
            if (M3_UNLIKELY(r == m3Err_continuationSuspended)) {
                return Continuation_RecordFrame(runtime, &frame);
            }
#    if d_m3RecordBacktraces
            FillBacktraceFunctionInfo(runtime, frame.entry.function);
#    else
            (void)frame.entry.function;
#    endif
        }
        return r;
#  endif
    }

    return r;
}


// Where a continuation's i-th incoming value goes. A continuation that has not
// started yet takes them as the arguments of its entry frame, laid out above
// its return slots; one that is suspended takes them in the slots its own
// suspend or switch left waiting.
static
void* ContinuationArgDest (IM3Continuation i_cont, u32 i_index)
{
    if (i_cont->state == cont_allocated) {
        IM3FuncType inner = i_cont->type ? i_cont->type->contFuncType : NULL;
        u16         numRets = inner ? GetFuncTypeNumResults(inner) : 0;

        return i_cont->valStack + (numRets + i_index) * c_ioSlotCount;
    }

    if (i_index < i_cont->numSuspendResults) {
        return i_cont->sp + i_cont->suspendResultOffsets[i_index];
    }

    return NULL;
}


// Hands a switch's payload to the peer, and after it the continuation that
// switched away - the reference that lets the peer come back this way.
static
void HandOverToPeer (IM3Runtime i_runtime, IM3Continuation io_peer, IM3Continuation i_from)
{
    u32 numArgs = i_runtime->numSuspendPayload;
    u32 base = (io_peer->state == cont_allocated) ? io_peer->boundArgsCount : 0;

    for (u32 i = 0; i < numArgs; ++i) {
        void* dest = ContinuationArgDest(io_peer, base + i);

        if (dest) {
            if (i_runtime->suspendPayloadIs64[i]) {
                *(u64*)dest = i_runtime->suspendPayload[i];
            } else {
                *(u32*)dest = (u32)i_runtime->suspendPayload[i];
            }
        }
    }

    void* refDest = ContinuationArgDest(io_peer, base + numArgs);

    if (refDest) {
        *(IM3Continuation*)refDest = i_from;
    }

    if (io_peer->state == cont_allocated) {
        io_peer->boundArgsCount += numArgs + 1;
    }
}


// resume, resume_throw and resume_throw_ref. The three differ only in what the
// continuation is handed to start with, which is what the mode immediate says,
// so everything after that - the handler table, the switch loop, the results -
// is written once.
// The innermost continuation of a capture: the one whose suspension point is
// at the bottom of it, and so the one whose waiting slots any values handed to
// the capture belong in. A resume the suspend travelled through leaves its
// record at the foot of the frame list, which is the link to follow.
static
IM3Continuation InnermostSuspended (IM3Continuation i_cont)
{
    while (i_cont->numFrames and (M3FrameKind) i_cont->frames[0].kind == frame_resume) {
        i_cont = i_cont->frames[0].resume.cont;
    }

    return i_cont;
}


// Runs a continuation under one handler table until something comes back that
// is not a switch, and reports which continuation was running when it did: a
// switch under this table takes the place of the one that switched away, and
// whatever is running is what the handler table belongs to.
static
m3ret_t RunUnderHandlers (IM3Runtime i_runtime, IM3Continuation* io_cont,
                          pc_t i_handlersPC, u32 i_numHandlers, M3MemoryHeader** io_mem)
{
    IM3Continuation cont = *io_cont;
    IM3Continuation parent = i_runtime->activeContinuation;
    IM3Memory       memory = m3MemInfo(*io_mem);
    m3ret_t         r;

    for (;;) {
#  if d_m3HasExceptionHandling
        // aborting a continuation that never started: there is no suspension
        // point to raise at, so the exception simply comes straight back out
        if (M3_UNLIKELY(cont->resumeThrow and cont->numFrames == 0)) {
            i_runtime->pendingException = cont->resumeThrow;
            cont->resumeThrow = NULL;
            cont->state = cont_consumed;
            r = m3Err_pendingException;
            break;
        }
#  endif
        if (cont->state == cont_allocated) {
            if (M3_UNLIKELY(not cont->entryFunction->compiled)) {
                r = CompileFunction(cont->entryFunction);

                if (M3_UNLIKELY(r)) {
                    break;
                }
            }
            cont->pc = cont->entryFunction->compiled;
            cont->sp = cont->valStack;
            cont->numFrames = 0;
        }

        cont->handlersPC = i_handlersPC;
        cont->numHandlers = i_numHandlers;
        cont->parent = parent;
        cont->state = cont_running;

        i_runtime->activeContinuation = cont;

        if (cont->numFrames) {
            // a suspended continuation: put back the native frames it unwound
            // through before landing on the suspension point. The count is
            // cleared first because a second suspend records into the same
            // array, starting over at index 0.
            i32 outermost = (i32)cont->numFrames - 1;
            cont->numFrames = 0;

            r = ReplayFrames(cont, outermost, *io_mem);
        } else {
            r = d_m3CallWithRegs(cont->pc, cont->sp, *io_mem, 0, 0.);
        }

        *io_mem = memory->mallocated;
        i_runtime->activeContinuation = parent;

        if (r != m3Err_continuationSuspended) {
            break;
        }

        cont->state = cont_suspended;

        // not a switch, so it is a suspend and this is as far as we take it
        if (not i_runtime->switchTarget or i_runtime->suspendHandlerCont != cont) {
            break;
        }

        IM3Continuation peer = i_runtime->switchTarget;
        i_runtime->switchTarget = NULL;

        HandOverToPeer(i_runtime, peer, cont);

        cont = peer;
    }

    *io_cont = cont;

    return r;
}


// Copies what a finished continuation left in its return slots into the slots
// the resume site set aside for them.
static
void TakeContinuationResults (IM3Continuation i_cont, m3stack_t i_sp, pc_t i_resultsPC, u32 i_numResults)
{
    for (u32 i = 0; i < i_numResults; ++i) {
        i32 resSlot = *(i32*)(i_resultsPC + 2 * i);
        u32 is64 = *(u32*)(i_resultsPC + 2 * i + 1);
        u32 srcIdx = i * c_ioSlotCount;

        if (is64) {
            *(u64*)(i_sp + resSlot) = *(u64*)(i_cont->valStack + srcIdx);
        } else {
            *(u32*)(i_sp + resSlot) = *(u32*)(i_cont->valStack + srcIdx);
        }
    }
}


// resume, resume_throw and resume_throw_ref. The three differ only in what the
// continuation is handed to start with, which is what the mode immediate says,
// so everything after that - the handler table, the switch loop, the results -
// is written once.
d_m3Op(Resume)
{
    IM3Runtime      runtime = m3MemRuntime(_mem);
    IM3FuncType     contFuncType = immediate(IM3FuncType);
    i32             contSlot = immediate(i32);
    u32             mode = immediate(u32);

    IM3Continuation cont = *(IM3Continuation*)(_sp + contSlot);
    if (M3_UNLIKELY(not cont)) {
        newTrap(m3Err_trapNullContinuationRef);
    }
    if (M3_UNLIKELY(cont->state == cont_consumed or cont->state == cont_running)) {
        newTrap(m3Err_trapContinuationConsumed);
    }

    IM3FuncType innerType = contFuncType->contFuncType ? contFuncType->contFuncType : contFuncType;
    u16         numRets = GetFuncTypeNumResults(innerType);

    if (mode == d_m3ResumeThrowRef) {
        i32 exnSlot = immediate(i32);

#  if d_m3HasExceptionHandling
        M3Exception* exception = *(M3Exception**)(_sp + exnSlot);

        if (M3_UNLIKELY(not exception)) {
            newTrap(m3Err_trapNullReference);
        }

        InnermostSuspended(cont)->resumeThrow = exception;
#  else
        (void)exnSlot;
        newTrap(m3Err_trapUnsupportedInstruction);
#  endif
    } else if (mode == d_m3ResumeThrow) {
        IM3Tag exnTag = immediate(IM3Tag);
        u32    numArgs = immediate(u32);

#  if d_m3HasExceptionHandling
        M3Exception* exception = NewException(runtime, exnTag, numArgs);

        if (M3_UNLIKELY(not exception)) {
            newTrap(m3Err_mallocFailed);
        }

        for (u32 i = 0; i < numArgs; ++i) {
            i32 offset = immediate(i32);
            u32 is64 = immediate(u32);

            exception->args[i] = is64 ? *(u64*)(_sp + offset) : *(u32*)(_sp + offset);
        }

        InnermostSuspended(cont)->resumeThrow = exception;
#  else
        (void)exnTag;
        (void)numArgs;
        newTrap(m3Err_trapUnsupportedInstruction);
#  endif
    } else {
        u32 numArgs = immediate(u32);

        if (cont->state == cont_allocated) {
            for (u32 i = 0; i < numArgs; ++i) {
                i32 argSlot = immediate(i32);
                u32 is64 = immediate(u32);
                u32 targetIdx = (numRets + cont->boundArgsCount + i) * c_ioSlotCount;
                if (is64) {
                    *(u64*)(cont->valStack + targetIdx) = *(u64*)(_sp + argSlot);
                } else {
                    *(u32*)(cont->valStack + targetIdx) = *(u32*)(_sp + argSlot);
                }
            }
            cont->boundArgsCount = 0;
        } else {
            // the waiting slots belong to whichever continuation holds the
            // suspension point, which is not this one if a resume was captured
            // along with it
            IM3Continuation inner = InnermostSuspended(cont);

            for (u32 i = 0; i < numArgs; ++i) {
                i32 argSlot = immediate(i32);
                u32 is64 = immediate(u32);
                u32 dstIdx = cont->boundArgsCount + i;
                if (dstIdx < inner->numSuspendResults) {
                    i32 dstOffset = inner->suspendResultOffsets[dstIdx];
                    if (is64) {
                        *(u64*)(inner->sp + dstOffset) = *(u64*)(_sp + argSlot);
                    } else {
                        *(u32*)(inner->sp + dstOffset) = *(u32*)(_sp + argSlot);
                    }
                }
            }
        }
    }

    u32  numHandlers = immediate(u32);
    pc_t handlersPC = _pc;
    _pc += numHandlers * 3;

    u32       numResults = immediate(u32);
    pc_t      resultsPC = _pc;

    IM3Memory memory = m3MemInfo(_mem);

    m3ret_t   r = RunUnderHandlers(runtime, &cont, handlersPC, numHandlers, &_mem);

    if (r == m3Err_none) {
        cont->state = cont_consumed;

        TakeContinuationResults(cont, _sp, resultsPC, numResults);

        _pc += numResults * 2;
        nextOp();
    } else if (r == m3Err_continuationSuspended) {
        if (M3_UNLIKELY(runtime->suspendHandlerCont != cont)) {
            if (M3_UNLIKELY(not runtime->activeContinuation)) {
                newTrap(m3Err_trapUnhandledControlTag);
            }
            M3Frame frame;
            frame.kind = frame_resume;
            frame.pc = resultsPC + numResults * 2;
            frame.sp = _sp;
            frame.memory = memory;
            frame.resume.cont = cont;
            frame.resume.handlersPC = handlersPC;
            frame.resume.numHandlers = numHandlers;
            frame.resume.resultsPC = resultsPC;
            frame.resume.numResults = numResults;

            return Continuation_RecordFrame(runtime, &frame);
        }

        IM3Continuation suspended = Continuation_ForkSuspended(runtime, cont);
        if (M3_UNLIKELY(not suspended)) {
            newTrap(m3Err_mallocFailed);
        }
        runtime->suspendedContinuation = suspended;

        if (runtime->suspendStubPC) {
            jumpOpDirect(runtime->suspendStubPC);
        } else {
            newTrap(m3Err_trapUnhandledControlTag);
        }
    } else {
        cont->state = cont_consumed;
        pushBacktraceFrame();
        forwardTrap(r);
    }
}


d_m3Op(ResumePayload)
{
    IM3Runtime      runtime = m3MemRuntime(_mem);

    // The payload comes from the suspend; the reference is to the whole of what
    // was captured, which is not the same continuation once the suspend has
    // travelled out through a resume that had nothing to say about its tag.
    IM3Continuation cont = runtime->suspendedContinuation;

    u32             numArgs = immediate(u32);
    for (u32 i = 0; i < numArgs; ++i) {
        i32 offset = immediate(i32);
        u32 is64 = immediate(u32);
        u64 val = (i < runtime->numSuspendPayload) ? runtime->suspendPayload[i] : 0;
        if (is64) {
            *(u64*)(_sp + offset) = val;
        } else {
            *(u32*)(_sp + offset) = (u32)val;
        }
    }

    i32 contOffset = immediate(i32);
    if (contOffset >= 0) {
        *(IM3Continuation*)(_sp + contOffset) = cont;
    }

    nextOp();
}


// switch is a suspend that names its own replacement. Rather than run the
// peer here - which would leave this native frame owning a continuation it is
// no longer running - it suspends the current continuation the ordinary way and
// hands the peer to the resume that installed the matching (on $e switch)
// handler. That resume is the delimiter the proposal asks for, and it is the
// frame that should be tracking whatever is running under it.
d_m3Op(Switch)
{
    IM3Runtime      runtime = m3MemRuntime(_mem);
    IM3Continuation cont = runtime->activeContinuation;

    if (M3_UNLIKELY(not cont)) {
        newTrap(m3Err_trapUnhandledControlTag);
    }

    skip_immediate(IM3FuncType);

    IM3Tag          tag = immediate(IM3Tag);
    i32             targetSlot = immediate(i32);

    IM3Continuation target = *(IM3Continuation*)(_sp + targetSlot);

    if (M3_UNLIKELY(not target)) {
        newTrap(m3Err_trapNullContinuationRef);
    }
    if (M3_UNLIKELY(target->state == cont_consumed or target->state == cont_running)) {
        newTrap(m3Err_trapContinuationConsumed);
    }

    IM3Continuation handlerCont = FindHandler(cont, tag, 1, NULL);

    if (M3_UNLIKELY(not handlerCont)) {
        newTrap(m3Err_trapUnhandledControlTag);
    }

    // the peer's arguments, and after them the room for the continuation
    // reference the resume will write once this one is safely suspended
    u32 numArgs = immediate(u32);

    runtime->numSuspendPayload = numArgs;

    for (u32 i = 0; i < numArgs; ++i) {
        i32 argSlot = immediate(i32);
        u32 is64 = immediate(u32);

        runtime->suspendPayload[i] = is64 ? *(u64*)(_sp + argSlot) : (u64) * (u32*)(_sp + argSlot);
        runtime->suspendPayloadIs64[i] = is64;
    }

    // where the values come back to, when something switches into this one
    u32 numResults = immediate(u32);

    cont->numSuspendResults = numResults;

    for (u32 i = 0; i < numResults; ++i) {
        cont->suspendResultOffsets[i] = immediate(i32);
        cont->suspendResultIs64[i] = immediate(u32);
    }

    runtime->suspendStubPC = NULL;
    runtime->suspendHandlerCont = handlerCont;
    runtime->switchTarget = target;
    runtime->suspendTag = tag;

    cont->pc = _pc;
    cont->sp = _sp;
    cont->r0 = _r0;
#  if d_m3HasFloat
    cont->fp0 = _fp0;
#  endif

    return m3Err_continuationSuspended;
}


#endif // d_m3HasStackSwitching


d_m3Op(Branch)
{
    jumpOp(*_pc);
}


d_m3Op(If_r)
{
    i32  condition = (i32)_r0;

    pc_t elsePC = immediate(pc_t);

    if (condition) {
        nextOp();
    } else {
        jumpOp(elsePC);
    }
}


d_m3Op(If_s)
{
    i32  condition = slot(i32);

    pc_t elsePC = immediate(pc_t);

    if (condition) {
        nextOp();
    } else {
        jumpOp(elsePC);
    }
}


d_m3Op(BranchTable)
{
    u32   branchIndex = slot(u32);           // branch index is always in a slot
    u32   numTargets = immediate(u32);

    pc_t* branches = (pc_t*)_pc;

    if (branchIndex > numTargets) {
        branchIndex = numTargets; // the default index
    }

    jumpOp(branches[branchIndex]);
}


#define d_m3SetRegisterSetSlot(TYPE, REG) \
d_m3Op  (SetRegister_##TYPE)            \
{                                       \
    TYPE * src = slot_ptr (TYPE);       \
    d_m3PreloadNext ();                 \
    REG = * src;                        \
    nextOpPreloaded ();                 \
}                                       \
                                        \
d_m3Op (SetSlot_##TYPE)                 \
{                                       \
    TYPE * dst = slot_ptr (TYPE);       \
    d_m3PreloadNext ();                 \
    * dst = (TYPE) REG;                 \
    nextOpPreloaded ();                 \
}                                       \
                                        \
d_m3Op (PreserveSetSlot_##TYPE)         \
{                                       \
    TYPE * stack     = slot_ptr (TYPE); \
    TYPE * preserve  = slot_ptr (TYPE); \
    d_m3PreloadNext ();                 \
                                        \
    * preserve = * stack;               \
    * stack = (TYPE) REG;               \
                                        \
    nextOpPreloaded ();                 \
}

d_m3SetRegisterSetSlot(i32, _r0)
d_m3SetRegisterSetSlot(i64, _r0)
#if d_m3HasFloat
d_m3SetRegisterSetSlot(f32, _fp0)
d_m3SetRegisterSetSlot(f64, _fp0)
#endif

d_m3Op(CopySlot_32)
{
    u32* dst = slot_ptr(u32);
    u32* src = slot_ptr(u32);
    d_m3PreloadNext();

    *dst = *src;

    nextOpPreloaded();
}


d_m3Op(PreserveCopySlot_32)
{
    u32* dest = slot_ptr(u32);
    u32* src = slot_ptr(u32);
    u32* preserve = slot_ptr(u32);
    d_m3PreloadNext();

    *preserve = *dest;
    *dest = *src;

    nextOpPreloaded();
}


d_m3Op(CopySlot_64)
{
    u64* dst = slot_ptr(u64);
    u64* src = slot_ptr(u64);
    d_m3PreloadNext();

    *dst = *src;                  // printf ("copy: %p <- %" PRIi64 " <- %p\n", dst, * dst, src);

    nextOpPreloaded();
}


d_m3Op(PreserveCopySlot_64)
{
    u64* dest = slot_ptr(u64);
    u64* src = slot_ptr(u64);
    u64* preserve = slot_ptr(u64);
    d_m3PreloadNext();

    *preserve = *dest;
    *dest = *src;

    nextOpPreloaded();
}


#if d_m3EnableOpTracing
//--------------------------------------------------------------------------------------------------------
d_m3Op(DumpStack)
{
    u32         opcodeIndex = immediate(u32);
    u32         stackHeight = immediate(u32);
    IM3Function function = immediate(IM3Function);

    cstr_t      funcName = (function) ? m3_GetFunctionName(function) : "";

    printf(" %4d ", opcodeIndex);
    printf(" %-25s     r0: 0x%016" PRIx64 "  i:%" PRIi64 "  u:%" PRIu64 "\n", funcName, _r0, _r0, _r0);
#  if d_m3HasFloat
    printf("                                    fp0: %" PRIf64 "\n", _fp0);
#  endif
    m3stack_t sp = _sp;

    for (u32 i = 0; i < stackHeight; ++i) {
        cstr_t kind = "";

        printf("%p  %5s  %2d: 0x%" PRIx64 "  i:%" PRIi64 "\n", sp, kind, i, (u64) * (sp), (i64) * (sp));

        ++sp;
    }
    printf("---------------------------------------------------------------------------------------------------------\n");

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


d_m3Select_i(i32, _r0)
d_m3Select_i(i64, _r0)


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
d_m3Select_f(f32, _fp0, r, _r0)
d_m3Select_f(f32, _fp0, s, slot(i32))

d_m3Select_f(f64, _fp0, r, _r0)
d_m3Select_f(f64, _fp0, s, slot(i32))
#endif

d_m3Op(Return)
{
    m3StackCheck();
    return m3Err_none;
}


d_m3Op(BranchIf_r)
{
    i32  condition = (i32)_r0;
    pc_t branch = immediate(pc_t);

    if (condition) {
        jumpOp(branch);
    } else {
        nextOp();
    }
}


d_m3Op(BranchIf_s)
{
    i32  condition = slot(i32);
    pc_t branch = immediate(pc_t);

    if (condition) {
        jumpOp(branch);
    } else {
        nextOp();
    }
}

#if d_m3FuseBranch

// fused compare+branch and compare+if: a compare retro-patched by the compiler when a br_if or if
// immediately consumes its result. operand shapes and immediate order match the plain compare
// (_rs/_sr/_ss); the branch/else target pointer is the last immediate.
#  define d_m3FusedCmpBranch(TYPE, NAME, OPER)                \
  d_m3Op(TYPE##_BranchIf_##NAME##_rs)                         \
  {                                                           \
      TYPE operand = slot (TYPE);                             \
      pc_t branch  = immediate (pc_t);                        \
      if (operand OPER ((TYPE) _r0))                          \
          jumpOp (branch);                                    \
      else nextOp ();                                         \
  }                                                           \
  d_m3Op(TYPE##_BranchIf_##NAME##_sr)                         \
  {                                                           \
      TYPE operand = slot (TYPE);                             \
      pc_t branch  = immediate (pc_t);                        \
      if (((TYPE) _r0) OPER operand)                          \
          jumpOp (branch);                                    \
      else nextOp ();                                         \
  }                                                           \
  d_m3Op(TYPE##_BranchIf_##NAME##_ss)                         \
  {                                                           \
      TYPE operand2 = slot (TYPE);                            \
      TYPE operand1 = slot (TYPE);                            \
      pc_t branch   = immediate (pc_t);                       \
      if (operand1 OPER operand2)                             \
          jumpOp (branch);                                    \
      else nextOp ();                                         \
  }                                                           \
  d_m3Op(TYPE##_If_##NAME##_rs)                               \
  {                                                           \
      TYPE operand = slot (TYPE);                             \
      pc_t elsePC  = immediate (pc_t);                        \
      if (operand OPER ((TYPE) _r0))                          \
          nextOp ();                                          \
      else jumpOp (elsePC);                                   \
  }                                                           \
  d_m3Op(TYPE##_If_##NAME##_sr)                               \
  {                                                           \
      TYPE operand = slot (TYPE);                             \
      pc_t elsePC  = immediate (pc_t);                        \
      if (((TYPE) _r0) OPER operand)                          \
          nextOp ();                                          \
      else jumpOp (elsePC);                                   \
  }                                                           \
  d_m3Op(TYPE##_If_##NAME##_ss)                               \
  {                                                           \
      TYPE operand2 = slot (TYPE);                            \
      TYPE operand1 = slot (TYPE);                            \
      pc_t elsePC   = immediate (pc_t);                       \
      if (operand1 OPER operand2)                             \
          nextOp ();                                          \
      else jumpOp (elsePC);                                   \
  }

d_m3FusedCmpBranch(i32, Equal, ==)
d_m3FusedCmpBranch(i32, NotEqual, !=)
d_m3FusedCmpBranch(i32, LessThan, <)
d_m3FusedCmpBranch(i32, GreaterThan, >)
d_m3FusedCmpBranch(i32, LessThanOrEqual, <=)
d_m3FusedCmpBranch(i32, GreaterThanOrEqual, >=)
d_m3FusedCmpBranch(u32, LessThan, <)
d_m3FusedCmpBranch(u32, GreaterThan, >)
d_m3FusedCmpBranch(u32, LessThanOrEqual, <=)
d_m3FusedCmpBranch(u32, GreaterThanOrEqual, >=)

// i32.eqz is unary: form _r has the operand in _r0 (no operand immediate), _s reads a slot
d_m3Op(i32_BranchIfEqz_r)
{
    pc_t branch = immediate(pc_t);
    if (((i32)_r0) == 0) {
        jumpOp(branch);
    } else {
        nextOp();
    }
}

d_m3Op(i32_BranchIfEqz_s)
{
    i32  operand = slot(i32);
    pc_t branch = immediate(pc_t);
    if (operand == 0) {
        jumpOp(branch);
    } else {
        nextOp();
    }
}

d_m3Op(i32_IfEqz_r)
{
    pc_t elsePC = immediate(pc_t);
    if (((i32)_r0) == 0) {
        nextOp();
    } else {
        jumpOp(elsePC);
    }
}

d_m3Op(i32_IfEqz_s)
{
    i32  operand = slot(i32);
    pc_t elsePC = immediate(pc_t);
    if (operand == 0) {
        nextOp();
    } else {
        jumpOp(elsePC);
    }
}

#endif // d_m3FuseBranch


d_m3Op(BranchIfPrologue_r)
{
    i32  condition = (i32)_r0;
    pc_t branch = immediate(pc_t);

    if (condition) {
        // this is the "prologue" that ends with
        // a plain branch to the actual target
        nextOp();
    } else {
        jumpOp(branch); // jump over the prologue
    }
}


d_m3Op(BranchIfPrologue_s)
{
    i32  condition = slot(i32);
    pc_t branch = immediate(pc_t);

    if (condition) {
        nextOp();
    } else {
        jumpOp(branch);
    }
}


d_m3Op(ContinueLoop)
{
    m3StackCheck();

    void* loopId = immediate(void*);
    return loopId;
}


d_m3Op(ContinueLoopIf)
{
    i32   condition = (i32)_r0;
    void* loopId = immediate(void*);

    if (condition) {
        return loopId;
    } else {
        nextOp();
    }
}


#if d_m3HasStackSwitching

// This is where execution can "escape" the M3 code and callback to the client / fiber switch
// OR it can go in the Loop operation. I think it's best to do here. adding code to the loop operation
// has the potential to increase its native-stack usage.

d_m3Op(ContinueLoop_Suspendable)
{
    m3StackCheck();

    IM3Runtime runtime = m3MemRuntime(_mem);
    if (M3_UNLIKELY(runtime->suspendRequested and runtime->activeContinuation)) {
        SuspendWithoutTag(runtime, runtime->activeContinuation, _pc - 1, _sp, d_m3ExpRegArgs(_r0, _fp0));

        return m3Err_continuationSuspended;
    }

    void* loopId = immediate(void*);
    return loopId;
}


d_m3Op(ContinueLoopIf_Suspendable)
{
    i32   condition = (i32)_r0;
    void* loopId = immediate(void*);

    if (condition) {
        IM3Runtime runtime = m3MemRuntime(_mem);
        if (M3_UNLIKELY(runtime->suspendRequested and runtime->activeContinuation)) {
            SuspendWithoutTag(runtime, runtime->activeContinuation, _pc - 2, _sp, d_m3ExpRegArgs(_r0, _fp0));

            return m3Err_continuationSuspended;
        }
        return loopId;
    } else {
        nextOp();
    }
}
#endif // d_m3HasStackSwitching


d_m3Op(Const32)
{
    u32 value = *(u32*)_pc++;
    slot(u32) = value;
    nextOp();
}


d_m3Op(Const64)
{
    u64 value = *(u64*)_pc;
    _pc += (M3_SIZEOF_PTR == 4) ? 2 : 1;
    slot(u64) = value;
    nextOp();
}

d_m3Op(Unsupported)
{
    newTrap(m3Err_trapUnsupportedInstruction);
}

d_m3Op(Unreachable)
{
    m3StackCheck();
    newTrap(m3Err_trapUnreachable);
}


d_m3Op(End)
{
    m3StackCheck();
    return m3Err_none;
}


d_m3Op(SetGlobal_s32)
{
    u32* global = immediate(u32*);
    *global = slot(u32);

    nextOp();
}


d_m3Op(SetGlobal_s64)
{
    u64* global = immediate(u64*);
    *global = slot(u64);

    nextOp();
}

#if d_m3HasFloat
d_m3Op(SetGlobal_f32)
{
    f32* global = immediate(f32*);
    *global = (f32)_fp0;

    nextOp();
}


d_m3Op(SetGlobal_f64)
{
    f64* global = immediate(f64*);
    *global = _fp0;

    nextOp();
}
#endif


#if d_m3HasMemory64

// Checks a 64-bit address and folds the memarg offset into it, leaving the
// effective address in a slot the access that follows reads as an ordinary
// 32-bit one, with an offset of zero. That keeps one op between a 64-bit
// memory and the whole load/store table, rather than a second copy of it.
//
// The spec computes the effective address as a u65 and traps if it is out of
// range. Here the address is first held against d_m3AddressLimit and the
// compiler has already refused to emit this for an offset above it, so their
// sum is below 2^53 and no wider arithmetic is needed. An address that then
// passes the bounds check is below the memory's length, which no allocator
// lets past 32 bits, so the truncation below is exact.
d_m3Op(CheckAddr64)
{
    u64  operand = slot(u64);
    u32* address = slot_ptr(u32);

    u64  offset = *(u64*)_pc;
    _pc += (M3_SIZEOF_PTR == 4) ? 2 : 1;

    if (M3_LIKELY(operand < d_m3AddressLimit)) {
        u64 effective = operand + offset;

        // the access itself checks its own size against what is left
        if (M3_LIKELY(effective < _mem->length)) {
            *address = (u32)effective;
            nextOp();
        }
    }

    d_outOfBounds;
}

#endif // d_m3HasMemory64


// Under d_m3GuardedMemory there is nothing to check: every address one of these
// operations can name lands inside the memory's reservation, and the part of it the
// memory does not have is not committed, so the access faults and RunCodeChecked
// turns that into the trap the spec asks for.
#if d_m3SkipMemoryBoundsCheck || d_m3GuardedMemory
#  define m3MemCheck(x) true
#else
#  define m3MemCheck(x) M3_LIKELY(x)
#endif

// memcpy here is to support non-aligned access on some platforms.

#define d_m3Load(REG,DEST_TYPE,SRC_TYPE)                \
d_m3Op(DEST_TYPE##_Load_##SRC_TYPE##_r)                 \
{                                                       \
    d_m3TracePrepare                                    \
    u32 offset = immediate (u32);                       \
    d_m3PreloadNext ();                                 \
    u64 operand = (u32) _r0;                            \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (SRC_TYPE) <= _mem->length     \
    )) {                                                \
        {                                               \
            u8* src8 = m3MemData(_mem) + operand;       \
            SRC_TYPE value;                             \
            memcpy(&value, src8, sizeof(value));        \
            M3_BSWAP_##SRC_TYPE(value);                 \
            REG = (DEST_TYPE)value;                     \
            d_m3TraceLoad(DEST_TYPE, operand, REG);     \
        }                                               \
        nextOpPreloaded ();                             \
    } else d_outOfBounds;                               \
}                                                       \
d_m3Op(DEST_TYPE##_Load_##SRC_TYPE##_s)                 \
{                                                       \
    d_m3TracePrepare                                    \
    u64 operand = slot (u32);                           \
    u32 offset = immediate (u32);                       \
    d_m3PreloadNext ();                                 \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (SRC_TYPE) <= _mem->length     \
    )) {                                                \
        {                                               \
            u8* src8 = m3MemData(_mem) + operand;       \
            SRC_TYPE value;                             \
            memcpy(&value, src8, sizeof(value));        \
            M3_BSWAP_##SRC_TYPE(value);                 \
            REG = (DEST_TYPE)value;                     \
            d_m3TraceLoad(DEST_TYPE, operand, REG);     \
        }                                               \
        nextOpPreloaded ();                             \
    } else d_outOfBounds;                               \
}

//  printf ("get: %d -> %d\n", operand + offset, (i64) REG);


#define d_m3Load_i(DEST_TYPE, SRC_TYPE) d_m3Load(_r0, DEST_TYPE, SRC_TYPE)
#define d_m3Load_f(DEST_TYPE, SRC_TYPE) d_m3Load(_fp0, DEST_TYPE, SRC_TYPE)

#if d_m3HasFloat
d_m3Load_f(f32, f32);
d_m3Load_f(f64, f64);
#endif

d_m3Load_i(i32, i8);
d_m3Load_i(i32, u8);
d_m3Load_i(i32, i16);
d_m3Load_i(i32, u16);
d_m3Load_i(i32, i32);

d_m3Load_i(i64, i8);
d_m3Load_i(i64, u8);
d_m3Load_i(i64, i16);
d_m3Load_i(i64, u16);
d_m3Load_i(i64, i32);
d_m3Load_i(i64, u32);
d_m3Load_i(i64, i64);

#if d_m3FoldSetLocal

// destination-folded loads: the value goes to a trailing destination slot instead of _r0.
// operand immediates keep d_m3Load's order; the destination slot is the last immediate
#  define d_m3LoadFold(REG, DEST_TYPE, SRC_TYPE)          \
  d_m3Op(DEST_TYPE##_Load_##SRC_TYPE##_r_f)               \
  {                                                       \
      d_m3TracePrepare                                    \
      u32 offset = immediate (u32);                       \
      DEST_TYPE * dest = slot_ptr (DEST_TYPE);            \
      d_m3PreloadNext ();                                 \
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
          DEST_TYPE result = (DEST_TYPE)value;            \
          * dest = result;  REG = result;                 \
          d_m3TraceLoad(DEST_TYPE, operand, result);      \
          nextOpPreloaded ();                             \
      } else d_outOfBounds;                               \
  }                                                       \
  d_m3Op(DEST_TYPE##_Load_##SRC_TYPE##_s_f)               \
  {                                                       \
      d_m3TracePrepare                                    \
      u64 operand = slot (u32);                           \
      u32 offset = immediate (u32);                       \
      DEST_TYPE * dest = slot_ptr (DEST_TYPE);            \
      d_m3PreloadNext ();                                 \
      operand += offset;                                  \
                                                          \
      if (m3MemCheck(                                     \
          operand + sizeof (SRC_TYPE) <= _mem->length     \
      )) {                                                \
          u8* src8 = m3MemData(_mem) + operand;           \
          SRC_TYPE value;                                 \
          memcpy(&value, src8, sizeof(value));            \
          M3_BSWAP_##SRC_TYPE(value);                     \
          DEST_TYPE result = (DEST_TYPE)value;            \
          * dest = result;  REG = result;                 \
          d_m3TraceLoad(DEST_TYPE, operand, result);      \
          nextOpPreloaded ();                             \
      } else d_outOfBounds;                               \
  }

d_m3LoadFold(_r0, i32, i8)
d_m3LoadFold(_r0, i32, u8)
d_m3LoadFold(_r0, i32, i16)
d_m3LoadFold(_r0, i32, u16)
d_m3LoadFold(_r0, i32, i32)

// The fold forms take the address from _r0 and write the result straight to the
// destination slot, so they never touch _fp0 and the float widths need no register form.
d_m3LoadFold(_r0, i64, i8)
d_m3LoadFold(_r0, i64, u8)
d_m3LoadFold(_r0, i64, i16)
d_m3LoadFold(_r0, i64, u16)
d_m3LoadFold(_r0, i64, i32)
d_m3LoadFold(_r0, i64, u32)
d_m3LoadFold(_r0, i64, i64)
#  if d_m3HasFloat
d_m3LoadFold(_fp0, f32, f32)
d_m3LoadFold(_fp0, f64, f64)
#  endif

#endif // d_m3FoldSetLocal

#define d_m3Store(REG, SRC_TYPE, DEST_TYPE)             \
d_m3Op  (SRC_TYPE##_Store_##DEST_TYPE##_rs)             \
{                                                       \
    d_m3TracePrepare                                    \
    u64 operand = slot (u32);                           \
    u32 offset = immediate (u32);                       \
    d_m3PreloadNext ();                                 \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (DEST_TYPE) <= _mem->length    \
    )) {                                                \
        {                                               \
            d_m3TraceStore(SRC_TYPE, operand, REG);     \
            u8* mem8 = m3MemData(_mem) + operand;       \
            DEST_TYPE val = (DEST_TYPE) REG;            \
            M3_BSWAP_##DEST_TYPE(val);                  \
            memcpy(mem8, &val, sizeof(val));            \
        }                                               \
        nextOpPreloaded ();                             \
    } else d_outOfBounds;                               \
}                                                       \
d_m3Op  (SRC_TYPE##_Store_##DEST_TYPE##_sr)             \
{                                                       \
    d_m3TracePrepare                                    \
    const SRC_TYPE value = slot (SRC_TYPE);             \
    u64 operand = (u32) _r0;                            \
    u32 offset = immediate (u32);                       \
    d_m3PreloadNext ();                                 \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (DEST_TYPE) <= _mem->length    \
    )) {                                                \
        {                                               \
            d_m3TraceStore(SRC_TYPE, operand, value);   \
            u8* mem8 = m3MemData(_mem) + operand;       \
            DEST_TYPE val = (DEST_TYPE) value;          \
            M3_BSWAP_##DEST_TYPE(val);                  \
            memcpy(mem8, &val, sizeof(val));            \
        }                                               \
        nextOpPreloaded ();                             \
    } else d_outOfBounds;                               \
}                                                       \
d_m3Op  (SRC_TYPE##_Store_##DEST_TYPE##_ss)             \
{                                                       \
    d_m3TracePrepare                                    \
    const SRC_TYPE value = slot (SRC_TYPE);             \
    u64 operand = slot (u32);                           \
    u32 offset = immediate (u32);                       \
    d_m3PreloadNext ();                                 \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (DEST_TYPE) <= _mem->length    \
    )) {                                                \
        {                                               \
            d_m3TraceStore(SRC_TYPE, operand, value);   \
            u8* mem8 = m3MemData(_mem) + operand;       \
            DEST_TYPE val = (DEST_TYPE) value;          \
            M3_BSWAP_##DEST_TYPE(val);                  \
            memcpy(mem8, &val, sizeof(val));            \
        }                                               \
        nextOpPreloaded ();                             \
    } else d_outOfBounds;                               \
}

// both operands can be in regs when storing a float
#define d_m3StoreFp(REG, TYPE)                          \
d_m3Op  (TYPE##_Store_##TYPE##_rr)                      \
{                                                       \
    d_m3TracePrepare                                    \
    u64 operand = (u32) _r0;                            \
    u32 offset = immediate (u32);                       \
    d_m3PreloadNext ();                                 \
    operand += offset;                                  \
                                                        \
    if (m3MemCheck(                                     \
        operand + sizeof (TYPE) <= _mem->length         \
    )) {                                                \
        {                                               \
            d_m3TraceStore(TYPE, operand, REG);         \
            u8* mem8 = m3MemData(_mem) + operand;       \
            TYPE val = (TYPE) REG;                      \
            M3_BSWAP_##TYPE(val);                       \
            memcpy(mem8, &val, sizeof(val));            \
        }                                               \
        nextOpPreloaded ();                             \
    } else d_outOfBounds;                               \
}


#define d_m3Store_i(SRC_TYPE, DEST_TYPE) d_m3Store(_r0, SRC_TYPE, DEST_TYPE)
#define d_m3Store_f(SRC_TYPE, DEST_TYPE) d_m3Store(_fp0, SRC_TYPE, DEST_TYPE) d_m3StoreFp (_fp0, SRC_TYPE);

#if d_m3HasFloat
d_m3Store_f(f32, f32)
d_m3Store_f(f64, f64)
#endif

d_m3Store_i(i32, u8)
d_m3Store_i(i32, i16)
d_m3Store_i(i32, i32)

d_m3Store_i(i64, u8)
d_m3Store_i(i64, i16)
d_m3Store_i(i64, i32)
d_m3Store_i(i64, i64)

#undef m3MemCheck


//---------------------------------------------------------------------------------------------------------------------
// debug/profiling
//---------------------------------------------------------------------------------------------------------------------
#if d_m3EnableOpTracing
d_m3RetSig debugOp (d_m3OpSig, cstr_t i_opcode)
{
    char name[100];
    strcpy(name, strstr(i_opcode, "op_") + 3);
    char* bracket = strstr(name, "(");
    if (bracket) {
        *bracket = 0;
    }

    puts(name);
    nextOpDirect();
}
#endif

#if d_m3EnableOpProfiling
d_m3RetSig profileOp (d_m3OpSig, cstr_t i_operationName)
{
    ProfileHit(i_operationName);

    nextOpDirect();
}

// Same, for an operation leaving by a branch: jumpOp hands the target pc in as _pc,
// so dispatching from here lands exactly where jumpOpDirect would have.
d_m3RetSig profileJumpOp (d_m3OpSig, cstr_t i_operationName)
{
    ProfileHit(i_operationName);

    nextOpDirect();
}
#endif

d_m3EndExternC

#endif // m3_exec_h
