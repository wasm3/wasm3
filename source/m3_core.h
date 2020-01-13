//
//  m3_core.h
//
//  Created by Steven Massey on 4/15/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_core_h
#define m3_core_h

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "m3.h"
#include "m3_config.h"

#if !defined(d_m3ShortTypesDefined)
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
#endif // d_m3ShortTypesDefined

typedef const void *            m3ret_t;
typedef const void *            voidptr_t;
typedef const char *            cstr_t;
typedef const char * const      ccstr_t;
typedef const u8 *              bytes_t;
typedef const u8 * const        cbytes_t;

typedef i64                     m3reg_t;
typedef u64 *                   m3stack_t;

typedef
const void * const  cvptr_t;

# define d_m3Log_parse d_m3LogParse         // required for m3logif
# define d_m3Log_stack d_m3LogWasmStack
# define d_m3Log_runtime d_m3LogRuntime
# define d_m3Log_exec d_m3LogExec
# define d_m3Log_emit d_m3LogEmit

# if d_m3LogOutput && defined (DEBUG)

#   define d_m3Log(CATEGORY, FMT, ...)                  printf (" %8s  |  " FMT, #CATEGORY, ##__VA_ARGS__);

#   if d_m3LogParse
#       define m3log_parse(CATEGORY, FMT, ...)          d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#   else
#       define m3log_parse(...) {}
#   endif

#   if d_m3LogCompile
#       define m3log_compile(CATEGORY, FMT, ...)        d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#   else
#       define m3log_compile(...) {}
#   endif

#   if d_m3LogWasmStack
#       define m3log_stack(CATEGORY, FMT, ...)          d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#   else
#       define m3log_stack(...) {}
#   endif

#   if d_m3LogEmit
#       define m3log_emit(CATEGORY, FMT, ...)           d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#   else
#       define m3log_emit(...) {}
#   endif

#   if d_m3LogCodePages
#       define m3log_code(CATEGORY, FMT, ...)           d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#   else
#       define m3log_code(...) {}
#   endif

#   if d_m3LogModule
#       define m3log_module(CATEGORY, FMT, ...)         d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#   else
#       define m3log_module(...) {}
#   endif

#   if d_m3LogRuntime
#       define m3log_runtime(CATEGORY, FMT, ...)        d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#   else
#       define m3log_runtime(...) {}
#   endif

#   if d_m3LogExec
#       define m3log_exec(CATEGORY, FMT, ...)           d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#   else
#       define m3log_exec(...) {}
#   endif

#   define m3log(CATEGORY, FMT, ...)                    m3log_##CATEGORY (CATEGORY, FMT "\n", ##__VA_ARGS__)
#   define m3logif(CATEGORY, STATEMENT)                 m3log_##CATEGORY (CATEGORY, ""); if (d_m3Log_##CATEGORY) { STATEMENT; printf ("\n"); }
# else
#   define m3log(CATEGORY, FMT, ...)                    {}
#   define m3logif(CATEGORY, STATEMENT)                 {}
# endif


# ifdef DEBUG
#   define d_m3Assert(ASS)      assert (ASS)
# else
#   define d_m3Assert(ASS)
# endif

typedef void /*const*/ *                    code_t;
typedef code_t const * /*__restrict__*/     pc_t;


typedef struct M3MemoryHeader
{
    IM3Runtime      runtime;
    void *          maxStack;
    size_t          length;
}
M3MemoryHeader;


typedef struct M3CodePageHeader
{
    struct M3CodePage *     next;

    u32                     lineIndex;
    u32                     numLines;
    u32                     sequence;       // this is just used for debugging; could be removed
    u32                     usageCount;
}
M3CodePageHeader;


#define d_m3CodePageFreeLinesThreshold      4       // max is probably: select _sss

#define d_m3MemPageSize                     65536

#define d_m3Reg0SlotAlias                   d_m3MaxFunctionStackHeight + 1
#define d_m3Fp0SlotAlias                    d_m3MaxFunctionStackHeight + 2

#define d_m3MaxNumFunctionConstants         60

#define d_m3MaxSaneUtf8Length               2000

#define d_externalKind_function             0
#define d_externalKind_table                1
#define d_externalKind_memory               2
#define d_externalKind_global               3

static const char * const c_waTypes []          = { "nil", "i32", "i64", "f32", "f64", "void", "void *" };
static const char * const c_waCompactTypes []   = { "0", "i", "I", "f", "F", "v", "*" };


#define m3Alloc(OPTR, STRUCT, NUM)              m3Malloc ((void **) OPTR, sizeof (STRUCT) * (NUM))
#define m3RellocArray(PTR, STRUCT, NEW, OLD)    m3Realloc ((PTR), sizeof (STRUCT) * (NEW), sizeof (STRUCT) * (OLD))
#define m3Free(P)                               { m3Free_impl((void*)(P)); P = NULL; }

# if d_m3VerboseLogs

M3Result m3Error (M3Result i_result, IM3Runtime i_runtime, IM3Module i_module, IM3Function i_function,
                  const char * const i_file, u32 i_lineNum, const char * const i_errorMessage, ...);

#  define _m3Error(RESULT, RT, MOD, FUN, FILE, LINE, FORMAT, ...) \
            m3Error (RESULT, RT, MOD, FUN, FILE, LINE, FORMAT, ##__VA_ARGS__)

# else
#  define _m3Error(RESULT, RT, MOD, FUN, FILE, LINE, FORMAT, ...) (RESULT)
# endif

#define ErrorRuntime(RESULT, RUNTIME, FORMAT, ...)      _m3Error (RESULT, RUNTIME, NULL, NULL,  __FILE__, __LINE__, FORMAT, ##__VA_ARGS__)
#define ErrorModule(RESULT, MOD, FORMAT, ...)           _m3Error (RESULT, MOD->runtime, MOD, NULL,  __FILE__, __LINE__, FORMAT, ##__VA_ARGS__)
#define ErrorCompile(RESULT, COMP, FORMAT, ...)         _m3Error (RESULT, COMP->runtime, COMP->module, NULL, __FILE__, __LINE__, FORMAT, ##__VA_ARGS__)

#if d_m3LogNativeStack
void        m3StackCheckInit        ();
void        m3StackCheck            ();
size_t      m3StackGetMax           ();
#else
#define     m3StackCheckInit()
#define     m3StackCheck()
#define     m3StackGetMax()         0
#endif

void        m3Abort                 (const char* message);
void        m3NotImplemented        (void);

void        m3Yield                 (void);

M3Result    m3Malloc                (void ** o_ptr, size_t i_size);
void *      m3Realloc               (void * i_ptr, size_t i_newSize, size_t i_oldSize);
void        m3Free_impl             (void * o_ptr);

M3Result    NormalizeType           (u8 * o_type, i8 i_convolutedWasmType);

bool        IsIntType               (u8 i_wasmType);
bool        IsFpType                (u8 i_wasmType);
bool        Is64BitType             (u8 i_m3Type);
u32         SizeOfType              (u8 i_m3Type);

M3Result    Read_u64                (u64 * o_value, const u8 ** io_bytes, cbytes_t i_end);
M3Result    Read_u32                (u32 * o_value, const u8 ** io_bytes, cbytes_t i_end);
M3Result    Read_f64                (f64 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    Read_f32                (f32 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    Read_u8                 (u8 * o_value, const u8 ** io_bytes, cbytes_t i_end);

M3Result    ReadLebUnsigned         (u64 * o_value, u32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLebSigned           (i64 * o_value, u32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLEB_u32             (u32 * o_value, bytes_t* io_bytes, cbytes_t i_end);
M3Result    ReadLEB_u7              (u8 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLEB_i7              (i8 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLEB_i32             (i32 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLEB_i64             (i64 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    Read_utf8               (cstr_t * o_utf8, bytes_t * io_bytes, cbytes_t i_end);

size_t      SPrintArg               (char * o_string, size_t i_n, m3stack_t i_sp, u8 i_type);

void        ReportError             (IM3Runtime io_runtime, IM3Module i_module, IM3Function i_function, ccstr_t i_errorMessage, ccstr_t i_file, u32 i_lineNum);

#endif // m3_core_h
