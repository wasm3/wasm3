//
//  m3_info.c
//  m3
//
//  Created by Steven Massey on 4/27/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_compile.h"

#include <assert.h>

void  m3_PrintM3Info  ()
{
    printf ("\n-- m3 configuration --------------------------------------------\n");
//  printf (" sizeof M3CodePage    : %lu bytes  (%d slots) \n", sizeof (M3CodePage), c_m3CodePageNumSlots);
    printf (" sizeof M3MemPage     : %d bytes              \n", c_m3MemPageSize);
    printf (" sizeof M3Compilation : %ld bytes              \n", sizeof (M3Compilation));
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

    printf (" stack-size: %lu   \n\n", i_runtime->numStackSlots * sizeof (m3reg_t));

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
        len = snprintf (o_string, i_n, "%d", * (i32 *) i_sp);
    else if (i_type == c_m3Type_i64)
        len = snprintf (o_string, i_n, "%ld", * i_sp);
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


typedef struct OpInfo
{
    IM3OpInfo   info;
    u8          opcode;
}
OpInfo;


OpInfo FindOperationInfo  (IM3Operation i_operation)
{
    OpInfo opInfo;
    M3_INIT(opInfo);

    for (u32 i = 0; i <= 0xff; ++i)
    {
        IM3OpInfo oi = & c_operations [i];

        if (oi->type != c_m3Type_void)
        {
            if (oi->operation_rs == i_operation or
                oi->operation_sr == i_operation or
                oi->operation_ss == i_operation)
            {
                opInfo.info = oi;
                opInfo.opcode = i;
                break;
            }
        }
        else break;
    }

    return opInfo;
}


#define fetch2(TYPE) (*(TYPE *) ((*o_pc)++))

void  Decode_Call  (char * o_string, u8 i_opcode, IM3OpInfo i_opInfo, pc_t * o_pc)
{
    void * function = fetch2 (void *);
    u16 stackOffset = fetch2 (u16);
    
    sprintf (o_string, "%p; stack-offset: %d", function, stackOffset);
}


void  Decode_Entry  (char * o_string, u8 i_opcode, IM3OpInfo i_opInfo, pc_t * o_pc)
{
    IM3Function function = fetch2 (IM3Function);
    
    sprintf (o_string, "%s", function->name);
}


#undef fetch
#define fetch(TYPE) (*(TYPE *) (pc++))

void  DecodeOperation  (char * o_string, u8 i_opcode, IM3OpInfo i_opInfo, pc_t * o_pc)
{
    pc_t pc = * o_pc;


    i32 offset;
    
    if (i_opcode == c_waOp_branchTable)
    {
        m3reg_t targets = fetch (m3reg_t);
        
        char str [1000];

        for (m3reg_t i = 0; i <targets; ++i)
        {
            pc_t addr = fetch (pc_t);
            sprintf (str, "%" PRIu64 ": %p, ", i, addr);
            strcat (o_string, str);
        }
        
        pc_t addr = fetch (pc_t);
        sprintf (str, "def: %p ", addr);
        strcat (o_string, str);
    }
    else if (i_opcode == 0xbf+1) // const
    {
        u64 value = fetch (u64); offset = fetch (i32);
        sprintf (o_string, " slot [%d] = %" PRIu64, offset, value);
    }
    else if (i_opcode == 0xc1) // entry
    {
        Decode_Entry (o_string, i_opcode, i_opInfo, o_pc);
    }
    else if (i_opcode == c_waOp_call)
        Decode_Call (o_string, i_opcode, i_opInfo, o_pc);

    #undef fetch

    * o_pc = pc;
}


# ifdef DEBUG
// WARNING/TODO: this isn't fully implemented. it blindly assumes each word is a Operation pointer
// and, if an operation happens to missing from the c_operations table it won't be recognized here
void  DumpCodePage  (IM3CodePage i_codePage, pc_t i_startPC)
{
        m3log (code, "code page seq: %d", i_codePage->info.sequence);
    
        pc_t pc = i_startPC ? i_startPC : GetPageStartPC (i_codePage);
        pc_t end = GetPagePC (i_codePage);

        m3log (code, "---------------------------------------------------------------------------------------");

        while (pc < end)
        {
            IM3Operation op = (IM3Operation) (* pc++);

            if (op)
            {
                OpInfo i = FindOperationInfo (op);

                if (i.info)
                {
                    char infoString [1000] = { 0 };
                    DecodeOperation (infoString, i.opcode, i.info, & pc);

                    m3log (code, "%p | %20s  %s", pc - 1, i.info->name, infoString);
                }
//                else break;
            }
//            else break;
        }

        m3log (code, "---------------------------------------------------------------------------------------");
}
# endif




