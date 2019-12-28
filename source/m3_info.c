//
//  m3_info.c
//  m3
//
//  Created by Steven Massey on 4/27/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_info.h"
#include "m3_emit.h"
#include "m3_compile.h"

void  m3_PrintM3Info  ()
{
    printf ("\n-- m3 configuration --------------------------------------------\n");
//  printf (" sizeof M3CodePage    : %zu bytes  (%d slots) \n", sizeof (M3CodePage), c_m3CodePageNumSlots);
    printf (" sizeof M3MemPage     : %u bytes              \n", c_m3MemPageSize);
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

    printf (" stack-size: %zu   \n\n", i_runtime->numStackSlots * sizeof (m3reg_t));

    u32 moduleIndex = 0;
    ForEachModule (i_runtime, (ModuleVisitor) v_PrintEnvModuleInfo, & moduleIndex);

    printf ("----------------------------------------------------------------\n\n");
}


cstr_t  GetTypeName  (u8 i_m3Type)
{
    if (i_m3Type < 7)
        return c_waTypes [i_m3Type];
    else
        return "?";
}


void  PrintFuncTypeSignature  (IM3FuncType i_funcType)
{
    printf ("(");

    u32 numArgs = i_funcType->numArgs;
    u8 * types = i_funcType->argTypes;

    for (u32 i = 0; i < numArgs; ++i)
    {
        if (i != 0)
            printf (", ");
        printf ("%s", GetTypeName (types [i]));
    }

    printf (") -> %s", GetTypeName (i_funcType->returnType));
}


size_t  SPrintArg  (char * o_string, size_t i_n, m3stack_t i_sp, u8 i_type)
{
    int len = 0;

    * o_string = 0;

    if      (i_type == c_m3Type_i32)
        len = snprintf (o_string, i_n, "%" PRIi32, * (i32 *) i_sp);
    else if (i_type == c_m3Type_i64)
        len = snprintf (o_string, i_n, "%" PRIi64, * i_sp);
    else if (i_type == c_m3Type_f32)
        len = snprintf (o_string, i_n, "%f",  * (f32 *) i_sp);
    else if (i_type == c_m3Type_f64)
        len = snprintf (o_string, i_n, "%lf", * (f64 *) i_sp);

    len = max (0, len);

    return len;
}


cstr_t  SPrintFunctionArgList  (IM3Function i_function, m3stack_t i_sp)
{
    static char string [256];

    char * s = string;
    ccstr_t e = string + sizeof(string) - 1;

    s += max (0, snprintf (s, e-s, "("));

    IM3FuncType funcType = i_function->funcType;
    if (funcType)
    {
        u32 numArgs = funcType->numArgs;
        u8 * types = funcType->argTypes;

        for (u32 i = 0; i < numArgs; ++i)
        {
            u8 type = types [i];

            s += max (0, snprintf (s, e-s, "%s: ", c_waTypes [type]));

            s += SPrintArg (s, e-s, i_sp + i, type);

            if (i != numArgs - 1)
                s += max (0, snprintf (s, e-s, ", "));
        }
    }
    else printf ("null signature");

    s += max (0, snprintf (s, e-s, ")"));

    return string;
}


OpInfo find_operation_info  (IM3Operation i_operation)
{
    OpInfo opInfo;
    M3_INIT(opInfo);

    for (u32 i = 0; i <= 0xff; ++i)
    {
        IM3OpInfo oi = & c_operations [i];

        if (oi->type != c_m3Type_void)
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
#define fetch(TYPE) (*(TYPE *) ((*o_pc)++))

#define d_m3Decoder(FUNC) void Decode_##FUNC (char * o_string, u8 i_opcode, IM3Operation i_operation, IM3OpInfo i_opInfo, pc_t * o_pc)

d_m3Decoder  (Call)
{
    void * function = fetch (void *);
    u16 stackOffset = fetch (u16);
    
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
    u16 slot = fetch (u16);
    
    sprintf (o_string, "slot: %" PRIu16 "; targets: ", slot);
    
//    IM3Function function = fetch2 (IM3Function);
    
    m3reg_t targets = fetch (m3reg_t);
    
    char str [1000];
    
    for (m3reg_t i = 0; i <targets; ++i)
    {
        pc_t addr = fetch (pc_t);
        sprintf (str, "%" PRIu64 "=%p, ", i, addr);
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
        d_m3Decode (0xc0,                  Const)
        d_m3Decode (0xc1,                  Entry)
        d_m3Decode (c_waOp_call,           Call)
        d_m3Decode (c_waOp_branch,         Branch)
        d_m3Decode (c_waOp_branchTable,    BranchTable)
        d_m3Decode (0x39,                  f64_Store)
    }
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
     -- the intial stack dumps originate from the CompileLocals () function, so these identifiers won't/can't be
     applied until this compilation stage is finished
     -- constants are not statically represented in the type stack (like args & constants) since they don't have/need
     write counts
     -- the number shown for static args and locals (value in wasmStack [i]) represents the write count for the variable
     -- (does Wasm ever write to an arg? I dunno/don't remember.)
     -- the number for the dynamic stack values represents the slot number.
     -- if the slot index points to arg, local or constant it's denoted with a lowercase 'a', 'l' or 'c'
     
     */
    
#   if d_m3LogOutput
    
    // for the assert at end of dump:
    i32 regAllocated [2] = { (i32) IsRegisterAllocated (o, 0), (i32) IsRegisterAllocated (o, 1) };
    
    // display whether r0 or fp0 is allocated. these should then also be reflected somewhere in the stack too.
    printf ("                                                        ");
    printf ("%s %s    ", regAllocated [0] ? "(r0)" : "    ", regAllocated [1] ? "(fp0)" : "     ");
    
    u32 numArgs = GetFunctionNumArgs (o->function);
    
    for (u32 i = 0; i < o->stackIndex; ++i)
    {
        if (i == o->firstConstSlotIndex)
            printf (" | ");                     // divide the static & dynamic portion of the stack
        
        //        printf (" %d:%s.", i, c_waTypes [o->typeStack [i]]);
        printf (" %s", c_waCompactTypes [o->typeStack [i]]);
        if (i < o->firstConstSlotIndex)
        {
            u16 writeCount = o->wasmStack [i];
            
            printf ((i < numArgs) ? "A" : "L");     // arg / local
            printf ("%d", (i32) writeCount);        // writeCount
        }
        else
        {
            u16 slot = o->wasmStack [i];
            
            if (IsRegisterLocation (slot))
            {
                bool isFp = IsFpRegisterLocation (slot);
                printf ("%s", isFp ? "f0" : "r0");
                
                regAllocated [isFp]--;
            }
            else
            {
                if (slot < o->firstSlotIndex)
                {
                    if (slot >= o->firstConstSlotIndex)
                        printf ("c");
                    else if (slot >= numArgs)
                        printf ("l");
                    else
                        printf ("a");
                }
                
                printf ("%d", (i32) slot);  // slot
            }
        }
        
        printf (" ");
    }
    
    for (u32 r = 0; r < 2; ++r)
        d_m3Assert (regAllocated [r] == 0);         // reg allocation & stack out of sync
    
#   endif
}


const char *  GetOpcodeIndentionString  (IM3Compilation o)
{
    i32 blockDepth = o->block.depth + 1;
    
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
    o->block.depth += 4;
    const char *indent = GetOpcodeIndentionString (o);
    o->block.depth -= 4;
    
    return indent;
}


void  log_opcode  (IM3Compilation o, u8 i_opcode)
{
    if (i_opcode == c_waOp_end or i_opcode == c_waOp_else)
        o->block.depth--;
    
#   ifdef DEBUG
        m3log (compile, "%4d | 0x%02x  %s %s", o->numOpcodes++, i_opcode, GetOpcodeIndentionString (o), c_operations [i_opcode].name);
#   else
        m3log (compile, "%4d | 0x%02x  %s", o->numOpcodes++, i_opcode, GetOpcodeIndentionString (o));
#   endif
        
    if (i_opcode == c_waOp_end or i_opcode == c_waOp_else)
        o->block.depth++;
}


void emit_stack_dump (IM3Compilation o)
{
#   if d_m3RuntimeStackDumps
    if (o->numEmits)
    {
        EmitOp          (o, op_DumpStack);
        EmitConstant    (o, o->numOpcodes);
        EmitConstant    (o, GetMaxExecSlot (o));
        EmitConstant    (o, (u64) o->function);
        
        o->numEmits = 0;
    }
#   endif
}


void  log_emit  (IM3Compilation o, IM3Operation i_operation)
{
# ifdef DEBUG
    OpInfo i = find_operation_info (i_operation);
    
    if (i.info)
    {
        printf ("%p: %s", GetPC (o),  i.info->name);
    }
    else printf ("not found: %p", i_operation);
# endif
}

