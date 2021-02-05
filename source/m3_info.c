//
//  m3_info.c
//
//  Created by Steven Massey on 4/27/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_info.h"
#include "m3_emit.h"
#include "m3_compile.h"
#include "m3_exec.h"

#ifdef DEBUG

typedef struct OpInfo
{
    IM3OpInfo   info;
    m3opcode_t  opcode;
}
OpInfo;

void  m3_PrintM3Info  ()
{
    printf ("\n-- m3 configuration --------------------------------------------\n");
//  printf (" sizeof M3CodePage    : %zu bytes  (%d slots) \n", sizeof (M3CodePage), c_m3CodePageNumSlots);
    printf (" sizeof M3MemPage     : %u bytes              \n", d_m3MemPageSize);
    printf (" sizeof M3Compilation : %zu bytes             \n", sizeof (M3Compilation));
    printf ("----------------------------------------------------------------\n\n");
}


void *  v_PrintEnvModuleInfo  (IM3Module i_module, u32 * io_index)
{
    printf (" module [%u]  name: '%s'; funcs: %d  \n", * io_index++, i_module->name, i_module->numFunctions);

    return NULL;
}


void  m3_PrintRuntimeInfo  (IM3Runtime i_runtime)
{
    printf ("\n-- m3 runtime -------------------------------------------------\n");

    printf (" stack-size: %zu   \n\n", i_runtime->numStackSlots * sizeof (m3slot_t));

    u32 moduleIndex = 0;
    ForEachModule (i_runtime, (ModuleVisitor) v_PrintEnvModuleInfo, & moduleIndex);

    printf ("----------------------------------------------------------------\n\n");
}


cstr_t  GetTypeName  (u8 i_m3Type)
{
    if (i_m3Type < 5)
        return c_waTypes [i_m3Type];
    else
        return "?";
}


cstr_t  SPrintFuncTypeSignature  (IM3FuncType i_funcType)
{
    static char string [256];

    sprintf (string, "(");

    for (u32 i = 0; i < i_funcType->numArgs; ++i)
    {
        if (i != 0)
            strcat (string, ", ");

        strcat (string, GetTypeName (d_FuncArgType(i_funcType, i)));
    }

    strcat (string, ") -> ");

    for (u32 i = 0; i < i_funcType->numRets; ++i)
    {
        if (i != 0)
            strcat (string, ", ");

        strcat (string, GetTypeName (d_FuncRetType(i_funcType, i)));
    }

    return string;
}


size_t  SPrintArg  (char * o_string, size_t i_n, m3stack_t i_sp, u8 i_type)
{
    int len = 0;

    * o_string = 0;

    if      (i_type == c_m3Type_i32)
        len = snprintf (o_string, i_n, "%" PRIi32, * (i32 *) i_sp);
    else if (i_type == c_m3Type_i64)
        len = snprintf (o_string, i_n, "%" PRIi64, * (i64 *) i_sp);
#if d_m3HasFloat
    else if (i_type == c_m3Type_f32)
        len = snprintf (o_string, i_n, "%f",  * (f32 *) i_sp);
    else if (i_type == c_m3Type_f64)
        len = snprintf (o_string, i_n, "%lf", * (f64 *) i_sp);
#endif

    len = M3_MAX (0, len);

    return len;
}


cstr_t  SPrintFunctionArgList  (IM3Function i_function, m3stack_t i_sp)
{
    int ret;
    static char string [256];

    char * s = string;
    ccstr_t e = string + sizeof(string) - 1;

    ret = snprintf (s, e-s, "(");
    s += M3_MAX (0, ret);

    m3stack_t argSp = i_sp;

    IM3FuncType funcType = i_function->funcType;
    if (funcType)
    {
        u32 numArgs = funcType->numArgs;

        for (u32 i = 0; i < numArgs; ++i)
        {
            u8 type = d_FuncArgType(funcType, i);

            ret = snprintf (s, e-s, "%s: ", c_waTypes [type]);
            s += M3_MAX (0, ret);

            s += SPrintArg (s, e-s, argSp + i, type);

            if (i != numArgs - 1) {
                ret = snprintf (s, e-s, ", ");
                s += M3_MAX (0, ret);
            }
        }
    }
    else printf ("null signature");

    ret = snprintf (s, e-s, ")");
    s += M3_MAX (0, ret);

    return string;
}

static
OpInfo find_operation_info  (IM3Operation i_operation)
{
    OpInfo opInfo = { NULL, 0 };

    if (!i_operation) return opInfo;

    // TODO: find also extended opcodes
    for (u32 i = 0; i <= 0xff; ++i)
    {
        IM3OpInfo oi = GetOpInfo(i);

        if (oi->type != c_m3Type_unknown)
        {
            for (u32 o = 0; o < 4; ++o)
            {
                if (oi->operations [o] == i_operation)
                {
                    opInfo.info = oi;
                    opInfo.opcode = i;
                    break;
                }
            }
        }
        else break;
    }

    return opInfo;
}


#undef fetch
#define fetch(TYPE) (* (TYPE *) ((*o_pc)++))

#define d_m3Decoder(FUNC) void Decode_##FUNC (char * o_string, u8 i_opcode, IM3Operation i_operation, IM3OpInfo i_opInfo, pc_t * o_pc)

d_m3Decoder  (Call)
{
    void * function = fetch (void *);
    i32 stackOffset = fetch (i32);

    sprintf (o_string, "%p; stack-offset: %d", function, stackOffset);
}


d_m3Decoder (Entry)
{
    IM3Function function = fetch (IM3Function);

    sprintf (o_string, "%s", function->name);
}


d_m3Decoder (f64_Store)
{
    if (i_operation == i_opInfo->operations [0])
    {
        u32 operand = fetch (u32);
        u32 offset = fetch (u32);

        sprintf (o_string, "offset= slot:%d + immediate:%d", operand, offset);
    }

//    sprintf (o_string, "%s", function->name);
}


d_m3Decoder  (Branch)
{
    void * target = fetch (void *);
    sprintf (o_string, "%p", target);
}

d_m3Decoder  (BranchTable)
{
    u32 slot = fetch (u32);

    sprintf (o_string, "slot: %" PRIu32 "; targets: ", slot);

//    IM3Function function = fetch2 (IM3Function);

    i32 targets = fetch (i32);

    char str [1000];

    for (i32 i = 0; i < targets; ++i)
    {
        pc_t addr = fetch (pc_t);
        sprintf (str, "%" PRIi32 "=%p, ", i, addr);
        strcat (o_string, str);
    }

    pc_t addr = fetch (pc_t);
    sprintf (str, "def=%p ", addr);
    strcat (o_string, str);
}


d_m3Decoder  (Const)
{
    u64 value = fetch (u64); i32 offset = fetch (i32);
    sprintf (o_string, " slot [%d] = %" PRIu64, offset, value);
}


#undef fetch

void  DecodeOperation  (char * o_string, u8 i_opcode, IM3Operation i_operation, IM3OpInfo i_opInfo, pc_t * o_pc)
{
    #define d_m3Decode(OPCODE, FUNC) case OPCODE: Decode_##FUNC (o_string, i_opcode, i_operation, i_opInfo, o_pc); break;

    switch (i_opcode)
    {
//        d_m3Decode (0xc0,                  Const)
//        d_m3Decode (0xc1,                  Entry)
        d_m3Decode (c_waOp_call,           Call)
        d_m3Decode (c_waOp_branch,         Branch)
        d_m3Decode (c_waOp_branchTable,    BranchTable)
        d_m3Decode (0x39,                  f64_Store)
    }

    #undef d_m3Decode
    #define d_m3Decode(FUNC) if (i_operation == op_##FUNC) Decode_##FUNC (o_string, i_opcode, i_operation, i_opInfo, o_pc);

    d_m3Decode (Entry)
}


# ifdef DEBUG
// WARNING/TODO: this isn't fully implemented. it blindly assumes each word is a Operation pointer
// and, if an operation happens to missing from the c_operations table it won't be recognized here
void  dump_code_page  (IM3CodePage i_codePage, pc_t i_startPC)
{
        m3log (code, "code page seq: %d", i_codePage->info.sequence);

        pc_t pc = i_startPC ? i_startPC : GetPageStartPC (i_codePage);
        pc_t end = GetPagePC (i_codePage);

        m3log (code, "---------------------------------------------------------------------------------------");

        while (pc < end)
        {
            pc_t operationPC = pc;
            IM3Operation op = (IM3Operation) (* pc++);

                OpInfo i = find_operation_info (op);

                if (i.info)
                {
                    char infoString [1000] = { 0 };

                    DecodeOperation (infoString, i.opcode, op, i.info, & pc);

                    m3log (code, "%p | %20s  %s", operationPC, i.info->name, infoString);
                }
                else
                    m3log (code, "%p | %p", operationPC, op);

        }

        m3log (code, "---------------------------------------------------------------------------------------");

        m3log (code, "free-lines: %d", i_codePage->info.numLines - i_codePage->info.lineIndex);
}
# endif


void  dump_type_stack  (IM3Compilation o)
{
    /* Reminders about how the stack works! :)
     -- args & locals remain on the type stack for duration of the function. Denoted with a constant 'A' and 'L' in this dump.
     -- the initial stack dumps originate from the CompileLocals () function, so these identifiers won't/can't be
     applied until this compilation stage is finished
     -- constants are not statically represented in the type stack (like args & constants) since they don't have/need
     write counts

     -- the number shown for static args and locals (value in wasmStack [i]) represents the write count for the variable

     -- (does Wasm ever write to an arg? I dunno/don't remember.)
     -- the number for the dynamic stack values represents the slot number.
     -- if the slot index points to arg, local or constant it's denoted with a lowercase 'a', 'l' or 'c'

     */

    // for the assert at end of dump:
    i32 regAllocated [2] = { (i32) IsRegisterAllocated (o, 0), (i32) IsRegisterAllocated (o, 1) };

    // display whether r0 or fp0 is allocated. these should then also be reflected somewhere in the stack too.
    d_m3Log(stack, "");
    printf ("                                                        ");
    printf ("%s %s    ", regAllocated [0] ? "(r0)" : "    ", regAllocated [1] ? "(fp0)" : "     ");

    for (u32 i = o->firstDynamicStackIndex; i < o->stackIndex; ++i)
    {
        printf (" %s", c_waCompactTypes [o->typeStack [i]]);

        u16 slot = o->wasmStack [i];

        if (IsRegisterLocation (slot))
        {
            bool isFp = IsFpRegisterLocation (slot);
            printf ("%s", isFp ? "f0" : "r0");

            regAllocated [isFp]--;
        }
        else
        {
            if (slot < o->firstDynamicSlotIndex)
            {
                if (slot >= o->firstConstSlotIndex)
                    printf ("c");
                else if (slot >= o->function->numArgSlots)
                    printf ("L");
                else
                    printf ("a");
            }

            printf ("%d", (i32) slot);  // slot
        }

        printf (" ");
    }
    printf ("\n");

    for (u32 r = 0; r < 2; ++r)
        d_m3Assert (regAllocated [r] == 0);         // reg allocation & stack out of sync
}


static const char *  GetOpcodeIndentionString  (i32 blockDepth)
{
    blockDepth += 1;

    if (blockDepth < 0)
        blockDepth = 0;

    static const char * s_spaces = ".......................................................................................";
    const char * indent = s_spaces + strlen (s_spaces);
    indent -= (blockDepth * 2);
    if (indent < s_spaces)
        indent = s_spaces;

    return indent;
}


const char *  get_indention_string  (IM3Compilation o)
{
    return GetOpcodeIndentionString (o->block.depth+4);
}


void  log_opcode  (IM3Compilation o, u8 i_opcode)
{
    i32 depth = o->block.depth;
    if (i_opcode == c_waOp_end or i_opcode == c_waOp_else)
        depth--;

#   ifdef DEBUG
        m3log (compile, "%4d | 0x%02x  %s %s", o->numOpcodes++, i_opcode, GetOpcodeIndentionString (depth), c_operations [i_opcode].name);
#   else
        m3log (compile, "%4d | 0x%02x  %s", o->numOpcodes++, i_opcode, GetOpcodeIndentionString (depth));
#   endif
}


void emit_stack_dump (IM3Compilation o)
{
# if d_m3EnableOpTracing
    if (o->numEmits)
    {
        EmitOp          (o, op_DumpStack);
        EmitConstant32  (o, o->numOpcodes);
        EmitConstant32  (o, GetMaxUsedSlotPlusOne(o));
        EmitPointer     (o, o->function);

        o->numEmits = 0;
    }
# endif
}


void  log_emit  (IM3Compilation o, IM3Operation i_operation)
{
# ifdef DEBUG
    OpInfo i = find_operation_info (i_operation);

    d_m3Log(emit, "");
    if (i.info)
    {
        printf ("%p: %s\n", GetPC (o),  i.info->name);
    }
    else printf ("not found: %p\n", i_operation);
# endif
}

#endif // DEBUG

