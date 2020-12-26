//
//  m3_exec.c
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_exec.h"
#include "m3_compile.h"


void  ReportError2  (IM3Function i_function, m3ret_t i_result)
{
    i_function->module->runtime->runtimeError = (M3Result)i_result;
}

d_m3OpDef  (GetGlobal_s32)
{
    u32 * global = immediate (u32 *);
    slot (u32) = * global;                        //  printf ("get global: %p %" PRIi64 "\n", global, *global);

    nextOp ();
}


d_m3OpDef  (GetGlobal_s64)
{
    u64 * global = immediate (u64 *);
    slot (u64) = * global;                        // printf ("get global: %p %" PRIi64 "\n", global, *global);

    nextOp ();
}


d_m3OpDef  (SetGlobal_i32)
{
    u32 * global = immediate (u32 *);
    * global = (u32) _r0;                         //  printf ("set global: %p %" PRIi64 "\n", global, _r0);

    nextOp ();
}


d_m3OpDef  (SetGlobal_i64)
{
    u64 * global = immediate (u64 *);
    * global = (u64) _r0;                         //  printf ("set global: %p %" PRIi64 "\n", global, _r0);

    nextOp ();
}


d_m3OpDef  (Call)
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
    else return r;
}


d_m3OpDef  (CallIndirect)
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
                        r = nextOpDirect ();
                    }
                }
            }
            else r = m3Err_trapIndirectCallTypeMismatch;
        }
        else r = m3Err_trapTableElementIsNull;
    }
    else r = m3Err_trapTableIndexOutOfRange;

    return r;
}


d_m3OpDef  (CallRawFunction)
{
    M3RawCall call = (M3RawCall) (* _pc++);
    IM3Function function = immediate (IM3Function);
    void * userdata = immediate (void *);

    m3ret_t possible_trap = call (m3MemRuntime(_mem), (u64 *)_sp, m3MemData(_mem), userdata);
    return possible_trap;
}


d_m3OpDef  (MemCurrent)
{
    IM3Memory memory            = m3MemInfo (_mem);

    _r0 = memory->numPages;

    nextOp ();
}


d_m3OpDef  (MemGrow)
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


// it's a debate: should the compilation be trigger be the caller or callee page.
// it's a much easier to put it in the caller pager. if it's in the callee, either the entire page
// has be left dangling or it's just a stub that jumps to a newly acquire page.  In Gestalt, I opted
// for the stub approach. Stubbing makes it easier to dynamically free the compilation. You can also
// do both.
d_m3OpDef  (Compile)
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
        result = nextOpDirect ();
    }
    else ReportError2 (function, result);

    return result;
}



d_m3OpDef  (Entry)
{
    d_m3ClearRegisters

    IM3Function function = immediate (IM3Function);

#if d_m3SkipStackCheck
    if (true)
#else
    if ((void *) ((m3slot_t *) _sp + function->maxStackSlots) < _mem->maxStack)
#endif
    {
                                                                m3log (exec, " enter %p > %s %s", _pc - 2, function->name ? function->name : ".unnamed", SPrintFunctionArgList (function, _sp));

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

        m3ret_t r = nextOpDirect ();

#       if d_m3LogExec
            char str [100] = { '!', 0 };

            if (not r)
                SPrintArg (str, 99, _sp, GetSingleRetType(function->funcType));

            m3log (exec, " exit  < %s %s %s   %s", function->name, function->funcType->numRets ? "->" : "", str, r ? (cstr_t)r : "");
#       elif d_m3LogStackTrace
            if (r)
                printf (" ** %s  %p\n", function->name, _sp);
#       endif

        return r;
    }
    else return m3Err_trapStackOverflow;
}


d_m3OpDef  (Loop)
{
    // regs are unused coming into a loop anyway
    // this reduces code size & stack usage
    d_m3ClearRegisters

    m3ret_t r;

    IM3Memory memory = m3MemInfo (_mem);

    do
    {
        r = nextOpDirect ();                     // printf ("loop: %p\n", r);
        // linear memory pointer needs refreshed here because the block it's looping over
        // can potentially invoke the grow operation.
        _mem = memory->mallocated;
    }
    while (r == _pc);

    return r;
}


d_m3OpDef  (Branch)
{
    return jumpOp (* _pc);
}


d_m3OpDef  (If_r)
{
    i32 condition = (i32) _r0;

    pc_t elsePC = immediate (pc_t);

    if (condition)
        nextOp ();
    else
        return jumpOp (elsePC);
}


d_m3OpDef  (If_s)
{
    i32 condition = slot (i32);

    pc_t elsePC = immediate (pc_t);

    if (condition)
        nextOp ();
    else
        return jumpOp (elsePC);
}


d_m3OpDef  (BranchTable)
{
    i32 branchIndex = slot (i32);           // branch index is always in a slot
    i32 numTargets  = immediate (i32);

    pc_t * branches = (pc_t *) _pc;

    if (branchIndex < 0 or branchIndex > numTargets)
        branchIndex = numTargets; // the default index

    return jumpOp (branches [branchIndex]);
}


#define d_m3SetRegisterSetSlot(TYPE, REG) \
d_m3OpDef  (SetRegister_##TYPE)         \
{                                       \
    REG = slot (TYPE);                  \
    nextOp ();                          \
}                                       \
                                        \
d_m3OpDef (SetSlot_##TYPE)              \
{                                       \
    slot (TYPE) = (TYPE) REG;           \
    nextOp ();                          \
}                                       \
                                        \
d_m3OpDef (PreserveSetSlot_##TYPE)      \
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

d_m3OpDef (CopySlot_32)
{
    u32 * dst = slot_ptr (u32);
    u32 * src = slot_ptr (u32);

    * dst = * src;

    nextOp ();
}


d_m3OpDef (PreserveCopySlot_32)
{
    u32 * dest      = slot_ptr (u32);
    u32 * src       = slot_ptr (u32);
    u32 * preserve  = slot_ptr (u32);

    * preserve = * dest;
    * dest = * src;

    nextOp ();
}


d_m3OpDef (CopySlot_64)
{
    u64 * dst = slot_ptr (u64);
    u64 * src = slot_ptr (u64);

    * dst = * src;                  // printf ("copy: %p <- %" PRIi64 " <- %p\n", dst, * dst, src);

    nextOp ();
}


d_m3OpDef (PreserveCopySlot_64)
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
d_m3OpDef  (DumpStack)
{
    u32 opcodeIndex         = immediate (u32);
    u32 stackHeight         = immediate (u32);
    IM3Function function    = immediate (IM3Function);

    cstr_t funcName = (function) ? function->name : "";

    printf (" %4d ", opcodeIndex);
    printf (" %-25s     r0: 0x%016" PRIx64 "  i:%" PRIi64 "  u:%" PRIu64 "\n", funcName, _r0, _r0, _r0);
    printf ("                                    fp0: %lf\n", _fp0);

    m3stack_t sp = _sp;

    for (u32 i = 0; i < stackHeight; ++i)
    {
        cstr_t kind = "";

        printf ("%p  %5s  %2d: 0x%" PRIx64 "  i:%" PRIi64 "\n", sp, kind, i, (u64) *(sp), (i64) *(sp));

        ++sp;
    }
    printf ("---------------------------------------------------------------------------------------------------------\n");

    return nextOpDirect();
}
#endif


# if d_m3EnableOpProfiling
//--------------------------------------------------------------------------------------------------------
static M3ProfilerSlot s_opProfilerCounts [d_m3ProfilerSlotMask + 1] = {};

void  ProfileHit  (cstr_t i_operationName)
{
    u64 ptr = (u64) i_operationName;

    M3ProfilerSlot * slot = & s_opProfilerCounts [ptr & d_m3ProfilerSlotMask];

    if (slot->opName)
    {
        if (slot->opName != i_operationName)
        {
            m3_Abort ("profiler slot collision; increase d_m3ProfilerSlotMask");
        }
    }

    slot->opName = i_operationName;
    slot->hitCount++;
}


void  m3_PrintProfilerInfo  ()
{
    M3ProfilerSlot dummy;
    M3ProfilerSlot * maxSlot = & dummy;

    do
    {
        maxSlot->hitCount = 0;

        for (u32 i = 0; i <= d_m3ProfilerSlotMask; ++i)
        {
            M3ProfilerSlot * slot = & s_opProfilerCounts [i];

            if (slot->opName)
            {
                if (slot->hitCount > maxSlot->hitCount)
                    maxSlot = slot;
            }
        }

        if (maxSlot->opName)
        {
            fprintf (stderr, "%13llu  %s\n", maxSlot->hitCount, maxSlot->opName);
            maxSlot->opName = NULL;
        }
    }
    while (maxSlot->hitCount);
}

# else

void  m3_PrintProfilerInfo  () {}

# endif
