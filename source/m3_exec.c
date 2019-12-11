//
//  m3_exec.c
//  M3: Massey Meta Machine
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_exec.h"
#include "m3_compile.h"


m3ret_t ReportOutOfBoundsMemoryError (pc_t i_pc, u8 * i_mem, u32 i_offset)
{
    M3MemoryHeader * info = (M3MemoryHeader*)(i_mem) - 1;
    u8 * mem8 = i_mem + i_offset;

    ErrorRuntime (c_m3Err_trapOutOfBoundsMemoryAccess, info->runtime,
				  "memory bounds: [%p %p); accessed: %p; offset: %u overflow: %zd bytes",
				  i_mem, info->end, mem8, i_offset, mem8 - (u8 *) info->end);

    return c_m3Err_trapOutOfBoundsMemoryAccess;
}


void  ReportError2  (IM3Function i_function, m3ret_t i_result)
{
    i_function->module->runtime->runtimeError = (M3Result)i_result;
}


d_m3OpDef  (Call)
{
    pc_t callPC                 = immediate (pc_t);
    i32 stackOffset             = immediate (i32);

    m3stack_t sp = _sp + stackOffset;

    m3ret_t r = Call (callPC, sp, _mem, d_m3OpDefaultArgs);

    if (r == 0)
        return nextOp ();
    else
        return r;
}


d_m3OpDef  (CallIndirect)
{
    IM3Module module            = immediate (IM3Module);
    IM3FuncType type            = immediate (IM3FuncType);
    i32 stackOffset             = immediate (i32);

    m3stack_t sp = _sp + stackOffset;

    i32 tableIndex = * (i32 *) (sp + type->numArgs);

    if (tableIndex >= 0 and (u32)tableIndex < module->table0Size)
    {
        m3ret_t r = c_m3Err_none;

        IM3Function function = module->table0 [tableIndex];

        if (function)
        {
            if (type->numArgs != function->funcType->numArgs)
            {
                return c_m3Err_trapIndirectCallTypeMismatch;
            }

            if (type->returnType != function->funcType->returnType)
            {
                return c_m3Err_trapIndirectCallTypeMismatch;
            }

            for (u32 argIndex = 0; argIndex < type->numArgs; ++argIndex)
            {
                if (type->argTypes[argIndex] != function->funcType->argTypes[argIndex])
                {
                    return c_m3Err_trapIndirectCallTypeMismatch;
                }
            }

            if (not function->compiled)
                r = Compile_Function (function);

            if (not r)
            {
                r = Call (function->compiled, sp, _mem, d_m3OpDefaultArgs);

                if (not r)
                    r = nextOp ();
            }
        }
        else r = "trap: table element is null";

        return r;
    }
    else return c_m3Err_trapTableIndexOutOfRange;
}


d_m3OpDef  (MemCurrent)
{
    IM3Runtime runtime            = immediate (IM3Runtime);

    _r0 = runtime->memory.numPages;

    return nextOp ();
}


d_m3OpDef  (MemGrow)
{
    IM3Runtime runtime            = immediate (IM3Runtime);

    IM3Memory memory = & runtime->memory;

    u32 numPagesToGrow = (u32) _r0;
    u32 requiredPages = memory->numPages + numPagesToGrow;
    _r0 = memory->numPages;

    M3Result r = ResizeMemory (runtime, requiredPages);
    if (r)
        _r0 = -1;
    
    return nextOp ();
}


// it's a debate: should the compilation be trigger be the caller or callee page.
// it's a much easier to put it in the caller pager. if it's in the callee, either the entire page
// has be left dangling or it's just a stub that jumps to a newly acquire page.  In Gestalt, I opted
// for the stub approach. Stubbing makes it easier to dynamically free the compilation. You can also
// do both.
d_m3OpDef  (Compile)
{
    rewrite (op_Call);

    IM3Function function        = immediate (IM3Function);

    m3ret_t result = c_m3Err_none;

    if (not function->compiled) // check to see if function was compiled since this operation was emitted.
        result = Compile_Function (function);

    if (not result)
    {
        // patch up compiled pc and call rewriten op_Call
        *((size_t *) --_pc) = (size_t) (function->compiled);
        --_pc;
        result = nextOp ();
    }
    else ReportError2 (function, result);

    return result;
}



d_m3OpDef  (Entry)
{
    IM3Function function = immediate (IM3Function);
    function->hits++;                                       m3log (exec, " enter %p > %s %s", _pc - 2, function->name, SPrintFunctionArgList (function, _sp));

    u32 numLocals = function->numLocals;

    m3stack_t stack = _sp + GetFunctionNumArgs (function);
    while (numLocals--)                                     // it seems locals need to init to zero (at least for optimized Wasm code)
        * (stack++) = 0;

    memcpy (stack, function->constants, function->numConstants * sizeof (u64));

    m3ret_t r = nextOp ();

#if d_m3LogExec
        u8 returnType = function->funcType->returnType;

        char str [100] = { '!', 0 };

        if (not r)
            SPrintArg (str, 99, _sp, function->funcType->returnType);

        m3log (exec, " exit  < %s %s %s   %s\n", function->name, returnType ? "->" : "", str, r ? r : "");
#endif

    return r;
}


#if d_m3RuntimeStackDumps
d_m3OpDef  (DumpStack)
{
    u32 opcodeIndex         = immediate (u32);
    u64 stackHeight         = immediate (u64);
    IM3Function function    = immediate (IM3Function);

    cstr_t funcName = (function) ? function->name : "";

    printf (" %4d ", opcodeIndex);
    printf (" %-25s     r0: 0x%016" PRIx64 "  i:%" PRIi64 "  u:%" PRIu64 "\n", funcName, _r0, _r0, _r0);
    printf ("                                     fp0: %lf  \n", _fp0);

    u64 * sp = _sp;

    for (u32 i = 0; i < stackHeight; ++i)
    {
        printf ("%016llx  ", (u64) sp);

        cstr_t kind = "";

        printf ("%5s  %2d: 0x%" PRIx64 " %" PRIi64 "\n", kind, i, (u64) *(sp), (i64) *sp);

        ++sp;
    }
    printf ("---------------------------------------------------------------------------------------------------------\n");

    return nextOpDirect();
}
#endif



# if d_m3EnableOpProfiling
M3ProfilerSlot s_opProfilerCounts [c_m3ProfilerSlotMask] = {};


void  ProfileHit  (cstr_t i_operationName)
{
    u64 ptr = (u64) i_operationName;

    M3ProfilerSlot * slot = & s_opProfilerCounts [ptr & c_m3ProfilerSlotMask];

    if (slot->opName)
    {
        if (slot->opName != i_operationName)
        {
            printf ("**** profiler slot collision; increase mask width\n");
            m3NotImplemented ();
        }
    }

    slot->opName = i_operationName;
    slot->hitCount++;
}
# endif


void  m3_PrintProfilerInfo  ()
{
    # if d_m3EnableOpProfiling
        for (u32 i = 0; i <= c_m3ProfilerSlotMask; ++i)
        {
            M3ProfilerSlot * slot = & s_opProfilerCounts [i];

            if (slot->opName)
                printf ("%13llu  %s\n", slot->hitCount, slot->opName);
        }
    # endif
}



d_m3OpDef  (Loop)
{
    m3ret_t r;

    M3Memory * memory = immediate (M3Memory *);
    // smassey: a downside of this ^^^^ (embedding a memory pointer in the codepage)
    // is that codepages are tied to specific runtime.
    // originally, i was hoping that multiple runtimes (threads) could share
    // compiled m3 code. with non-Windows calling conventions, a new
    // "IM3Runtime _runtime" argument could be added to operations to detach
    // the execution context from the codepage.
    
    do
    {
        // linear memory pointer needs refreshed here because the block it's loop over
        // can potentially invoke the grow operator.
        _mem = memory->wasmPages;
        r = nextOp ();                     // printf ("loop: %p\n", r);
    }
    while (r == _pc);

    return r;
}


d_m3OpDef  (If_r)
{
    i32 condition = (i32) _r0;

    skip_immediate (pc_t);                      // empty preservation chain

    pc_t elsePC = immediate (pc_t);

    if (condition)
        return nextOp ();
    else
        return jumpOp (elsePC);
}


d_m3OpDef  (If_s)
{
    i32 condition = slot (i32);

    skip_immediate (pc_t);                      // empty preservation chain

    pc_t elsePC = immediate (pc_t);

    if (condition)
        return nextOp ();
    else
        return jumpOp (elsePC);
}


d_m3OpDef  (IfPreserve)
{
    i32 condition = (i32) _r0;

    pc_t p = immediate (pc_t);
    jumpOp (p);

    pc_t elsePC = immediate (pc_t);         //printf ("else: %p\n", elsePC);

    if (condition)
        return nextOp ();
    else
        return jumpOp (elsePC);
}


d_m3OpDef  (BranchTable)
{
    i32 branchIndex = slot (i32);           // branch index is always in a slot
    
    u32 numTargets  = immediate (u32);
    
    pc_t * branches = (pc_t *) _pc;
    
    if (branchIndex < 0 or branchIndex > numTargets)
        branchIndex = numTargets; // the default index
    
    return jumpOp (branches [branchIndex]);
}

