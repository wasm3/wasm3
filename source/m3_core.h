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

#include "wasm3.h"
#include "m3_config.h"

# if defined(__cplusplus)
#   define d_m3BeginExternC     extern "C" {
#   define d_m3EndExternC       }
# else
#   define d_m3BeginExternC
#   define d_m3EndExternC
# endif

d_m3BeginExternC

#define d_m3ImplementFloat (d_m3HasFloat || d_m3NoFloatDynamic)

#if !defined(d_m3ShortTypesDefined)

typedef uint64_t        u64;
typedef int64_t         i64;
typedef uint32_t        u32;
typedef int32_t         i32;
typedef uint16_t        u16;
typedef int16_t         i16;
typedef uint8_t         u8;
typedef int8_t          i8;

#if d_m3ImplementFloat
typedef double          f64;
typedef float           f32;
#endif

#endif // d_m3ShortTypesDefined

#define PRIf32          "f"
#define PRIf64          "lf"

typedef const void *            m3ret_t;
typedef const void *            voidptr_t;
typedef const char *            cstr_t;
typedef const char * const      ccstr_t;
typedef const u8 *              bytes_t;
typedef const u8 * const        cbytes_t;

typedef u16                     m3opcode_t;
#if d_m3HasTypedRefs
typedef u16                     m3type_t;       // a value type, see the d_m3Type_* bits below
#else
typedef u8                      m3type_t;       // only plain M3ValueType values exist
#endif

typedef i64                     m3reg_t;

# if d_m3Use32BitSlots
typedef u32                     m3slot_t;
# else
typedef u64                     m3slot_t;
# endif

typedef m3slot_t *              m3stack_t;

typedef
const void * const  cvptr_t;

# if defined (DEBUG)

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

#   define m3log(CATEGORY, FMT, ...)                    m3log_##CATEGORY (CATEGORY, FMT "\n", ##__VA_ARGS__)
# else
#   define d_m3Log(CATEGORY, FMT, ...)                  {}
#   define m3log(CATEGORY, FMT, ...)                    {}
# endif


# if defined(ASSERTS) || (defined(DEBUG) && !defined(NASSERTS))
#   define d_m3Assert(ASS)  if (!(ASS)) { printf("Assertion failed at %s:%d : %s\n", __FILE__, __LINE__, #ASS); fflush(stdout); abort(); }
# else
#   define d_m3Assert(ASS)
# endif

typedef void /*const*/ *                    code_t;
typedef code_t const * /*__restrict__*/     pc_t;


// Sits immediately before the linear memory it describes, so m3MemData() is
// just (header + 1). The interpreter carries a pointer to one of these in the
// _mem pseudo-register, and reaches everything else about the memory - its page
// counts, its owning runtime - through the back-pointers here. 'memory' is what
// makes that possible without going through the runtime, which is the
// prerequisite for a runtime holding more than one memory.
typedef struct M3MemoryHeader
{
    IM3Runtime          runtime;
    void *              maxStack;
    size_t              length;
    struct M3Memory *   memory;         // the M3Memory this block belongs to
}
M3MemoryHeader;

struct M3CodeMappingPage;

typedef struct M3CodePageHeader
{
    struct M3CodePage *           next;

    u32                           lineIndex;
    u32                           numLines;
    u32                           sequence;       // this is just used for debugging; could be removed
    u32                           usageCount;

# if d_m3RecordBacktraces
    struct M3CodeMappingPage *    mapping;
# endif // d_m3RecordBacktraces
}
M3CodePageHeader;


#define d_m3CodePageFreeLinesThreshold      7+2       // max is: ReturnCallIndirect (op + 6 immediates) + 2 for bridge

#define d_m3DefaultMemPageSize              65536

// The ceiling on a linear memory address, and so on the size of a linear
// memory. No allocator reaches anywhere near it - a memory is one contiguous
// realloc'd block, and d_m3MaxLinearMemoryPages caps it far lower - so an
// address at or above this is out of bounds no matter which memory it names.
//
// That is what a 64-bit address is checked against before anything is added to
// it. Once both the address and the memarg offset are known to be below 2^52,
// their sum is below 2^53 and cannot wrap, so the u65 effective address the
// spec calls for can be computed in a plain u64 - and stays well inside the
// positive range of m3reg_t, which is signed. Anything at or above the limit
// traps, which is what an address that far out would do anyway.
#define d_m3AddressLimitBits                52
#define d_m3AddressLimit                    (UINT64_C(1) << d_m3AddressLimitBits)

#define d_m3Reg0SlotAlias                   60000
#define d_m3Fp0SlotAlias                    (d_m3Reg0SlotAlias + 2)

#if d_m3HasTypedRefs
// a heap type index has to fit in the spare bits of an m3type_t, see below
#  define d_m3MaxSaneTypesCount             8190
#else
// M3Environment.numFuncTypes is the u16 that hands out M3FuncType.canonicalIndex,
// so the count has to stay below 65536 here as well
#  define d_m3MaxSaneTypesCount             65500
#endif
#define d_m3MaxSaneFunctionsCount           1000000
#define d_m3MaxSaneImportsCount             100000
#define d_m3MaxSaneExportsCount             100000
#define d_m3MaxSaneGlobalsCount             1000000
#define d_m3MaxSaneTagsCount                100000
#define d_m3MaxSaneElementSegments          10000000
#define d_m3MaxSaneDataSegments             100000
#define d_m3MaxSaneTableSize                10000000
#define d_m3MaxSaneMemoriesCount            10000
#if d_m3HasRefTypes
#  define d_m3MaxSaneTableCount             1000
#else
#  define d_m3MaxSaneTableCount             1           // MVP: a single table
#endif
#define d_m3MaxSaneUtf8Length               10000
#define d_m3MaxSaneFunctionArgRetCount      1000    // still insane, but whatever

#define d_externalKind_function             0
#define d_externalKind_table                1
#define d_externalKind_memory               2
#define d_externalKind_global               3
#define d_externalKind_tag                  4

// compact-import-section: these stand where an externtype would, after an empty
// item name, and introduce a run of imports sharing one module name. 0x7f gives
// each item its own externtype, 0x7e shares a single one across the whole run.
#define d_compactImports_perItemType        0x7f
#define d_compactImports_sharedType         0x7e

// the negated wasm encodings of the abstract reference types, as NormalizeType()
// stores them
#define d_waType_funcref                    16
#define d_waType_externref                  17

// exception handling: exnref is wasm-encoded as -0x17
#define d_waType_exnref                     23

// function-references: a reference type spelled out as (ref ht) / (ref null ht),
// where the abstract heap types reuse the funcref and externref encodings
#define d_waEncode_ref                      0x64
#define d_waEncode_refNull                  0x63

// indexed by M3ValueType: c_m3Type_count entries for the concrete types, plus
// one more for c_m3Type_unknown, which sits right after them
static const char * const c_waTypes []          = { "nil", "i32", "i64", "f32", "f64", "v128", "funcref", "externref", "exnref", "unknown" };
static const char * const c_waCompactTypes []   = { "_", "i", "I", "f", "F", "V", "r", "R", "e", "?" };


# if d_m3VerboseErrorMessages

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
int         m3StackGetMax           ();
#else
#define     m3StackCheckInit()
#define     m3StackCheck()
#define     m3StackGetMax()         0
#endif

#if d_m3LogTimestamps
#define     PRIts                   "%llu"
uint64_t    m3_GetTimestamp         ();
#else
#define     PRIts                   "%s"
#define     m3_GetTimestamp()       ""
#endif

void        m3_Abort                (const char* message);
void *      m3_Malloc_Impl          (size_t i_size);
void *      m3_Realloc_Impl         (void * i_ptr, size_t i_newSize, size_t i_oldSize);
void        m3_Free_Impl            (void * i_ptr);
void *      m3_CopyMem              (const void * i_from, size_t i_size);

#if d_m3LogHeapOps

// Tracing format: timestamp;heap:OpCode;name;size(bytes);new items;new ptr;old items;old ptr

static inline void * m3_AllocStruct_Impl(ccstr_t name, size_t i_size) {
    void * result = m3_Malloc_Impl(i_size);
    fprintf(stderr, PRIts ";heap:AllocStruct;%s;%zu;;%p;;\n", m3_GetTimestamp(), name, i_size, result);
    return result;
}

static inline void * m3_AllocArray_Impl(ccstr_t name, size_t i_num, size_t i_size) {
    void * result = m3_Malloc_Impl(i_size * i_num);
    fprintf(stderr, PRIts ";heap:AllocArr;%s;%zu;%zu;%p;;\n", m3_GetTimestamp(), name, i_size, i_num, result);
    return result;
}

static inline void * m3_ReallocArray_Impl(ccstr_t name, void * i_ptr_old, size_t i_num_new, size_t i_num_old, size_t i_size) {
    void * result = m3_Realloc_Impl (i_ptr_old, i_size * i_num_new, i_size * i_num_old);
    fprintf(stderr, PRIts ";heap:ReallocArr;%s;%zu;%zu;%p;%zu;%p\n", m3_GetTimestamp(), name, i_size, i_num_new, result, i_num_old, i_ptr_old);
    return result;
}

static inline void * m3_Malloc (ccstr_t name, size_t i_size) {
    void * result = m3_Malloc_Impl (i_size);
    fprintf(stderr, PRIts ";heap:AllocMem;%s;%zu;;%p;;\n", m3_GetTimestamp(), name, i_size, result);
    return result;
}
static inline void * m3_Realloc (ccstr_t name, void * i_ptr, size_t i_newSize, size_t i_oldSize) {
    void * result = m3_Realloc_Impl (i_ptr, i_newSize, i_oldSize);
    fprintf(stderr, PRIts ";heap:ReallocMem;%s;;%zu;%p;%zu;%p\n", m3_GetTimestamp(), name, i_newSize, result, i_oldSize, i_ptr);
    return result;
}

#define     m3_AllocStruct(STRUCT)                  (STRUCT *)m3_AllocStruct_Impl  (#STRUCT, sizeof (STRUCT))
#define     m3_AllocArray(STRUCT, NUM)              (STRUCT *)m3_AllocArray_Impl   (#STRUCT, NUM, sizeof (STRUCT))
#define     m3_ReallocArray(STRUCT, PTR, NEW, OLD)  (STRUCT *)m3_ReallocArray_Impl (#STRUCT, (void *)(PTR), (NEW), (OLD), sizeof (STRUCT))
#define     m3_Free(P)                              do { void* p = (void*)(P);                                  \
                                                        if (p) { fprintf(stderr, PRIts ";heap:FreeMem;;;;%p;\n", m3_GetTimestamp(), p); }     \
                                                        m3_Free_Impl (p); (P) = NULL; } while(0)
#else
#define     m3_Malloc(NAME, SIZE)                   m3_Malloc_Impl(SIZE)
#define     m3_Realloc(NAME, PTR, NEW, OLD)         m3_Realloc_Impl(PTR, NEW, OLD)
#define     m3_AllocStruct(STRUCT)                  (STRUCT *)m3_Malloc_Impl (sizeof (STRUCT))
#define     m3_AllocArray(STRUCT, NUM)              (STRUCT *)m3_Malloc_Impl (sizeof (STRUCT) * (NUM))
#define     m3_ReallocArray(STRUCT, PTR, NEW, OLD)  (STRUCT *)m3_Realloc_Impl ((void *)(PTR), sizeof (STRUCT) * (NEW), sizeof (STRUCT) * (OLD))
#define     m3_Free(P)                              do { m3_Free_Impl ((void*)(P)); (P) = NULL; } while(0)
#endif

// A value type as the parser, compiler and validator carry it. The plain
// M3ValueType values keep their encoding, so every comparison against a
// c_m3Type_* constant still means exactly what it did. Only a typed function
// reference - (ref $t) or (ref null $t) - needs more room, and it spends it on
// the canonical index of the function type it points at. Fitting that index
// here is why d_m3MaxSaneTypesCount is 8190 rather than something rounder.
//
//   bit 15     spelled-out reference type rather than a plain M3ValueType
//   bit 14     non-null, i.e. (ref ht) rather than (ref null ht)
//   bit 13     heap type extern rather than func
//   bits 12-0  canonical function type index, or d_m3Type_heapAbstract

#if d_m3HasTypedRefs

#define d_m3Type_ref                0x8000u
#define d_m3Type_refNonNull         0x4000u
#define d_m3Type_refExtern          0x2000u
#define d_m3Type_heapMask           0x1FFFu
#define d_m3Type_heapAbstract       0x1FFFu     // plain 'func' or 'extern'

// The storage type a value of this type occupies: a reference of any shape is
// just a funcref or an externref once the type checking is done with.
u8          BaseTypeOf              (m3type_t i_type);
bool        IsSubTypeOf             (m3type_t i_sub, m3type_t i_super);

static inline bool  IsSpelledRefType (m3type_t i_type)  { return (i_type & d_m3Type_ref) != 0; }
static inline bool  IsNullableRef    (m3type_t i_type)  { return (i_type & d_m3Type_refNonNull) == 0; }
static inline u32   HeapTypeOf       (m3type_t i_type)
{
    return IsSpelledRefType (i_type) ? (i_type & d_m3Type_heapMask) : d_m3Type_heapAbstract;
}

#else // no spelled-out reference types, so every value type is its own storage
      // type and subtyping collapses back to equality

static inline u8    BaseTypeOf       (m3type_t i_type)  { return (u8) i_type; }
static inline bool  IsSubTypeOf      (m3type_t i_sub, m3type_t i_super) { return i_sub == i_super; }
static inline bool  IsSpelledRefType (m3type_t i_type)  { (void) i_type; return false; }
static inline bool  IsNullableRef    (m3type_t i_type)  { (void) i_type; return true; }

#endif // d_m3HasTypedRefs

M3Result    NormalizeType           (u8 * o_type, i8 i_convolutedWasmType);

// These all speak in terms of how a value is stored, so they reduce a
// spelled-out reference type to its funcref / externref base first.
bool        IsIntType               (m3type_t i_wasmType);
bool        IsFpType                (m3type_t i_wasmType);
bool        IsRefType               (m3type_t i_m3Type);
bool        Is64BitType             (m3type_t i_m3Type);
u32         SizeOfType              (m3type_t i_m3Type);

M3Result    Read_u64                (u64 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    Read_u32                (u32 * o_value, bytes_t * io_bytes, cbytes_t i_end);
#if d_m3ImplementFloat
M3Result    Read_f64                (f64 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    Read_f32                (f32 * o_value, bytes_t * io_bytes, cbytes_t i_end);
#endif
M3Result    Read_u8                 (u8  * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    Read_opcode             (m3opcode_t * o_value, bytes_t  * io_bytes, cbytes_t i_end);

M3Result    ReadLebUnsigned         (u64 * o_value, u32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLebSigned           (i64 * o_value, u32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLEB_u32             (u32 * o_value, bytes_t * io_bytes, cbytes_t i_end);

// The memarg immediate of a load or store: an alignment hint, then a static
// offset. Multi-memory reinterprets the alignment as a bitfield -- bit 6 says a
// memory index sits between the two -- so the hint is returned with that bit
// masked off and the index defaults to 0 when it is clear.
M3Result    ReadMemoryArg           (u32 * o_align, u32 * o_memoryIdx, u64 * o_offset, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLEB_u7              (u8  * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLEB_i7              (i8  * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLEB_i32             (i32 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    ReadLEB_i64             (i64 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result    Read_utf8               (cstr_t * o_utf8, bytes_t * io_bytes, cbytes_t i_end);

cstr_t      SPrintValue             (void * i_value, u8 i_type);
size_t      SPrintArg               (char * o_string, size_t i_stringBufferSize, voidptr_t i_sp, u8 i_type);

void        ReportError             (IM3Runtime io_runtime, IM3Module i_module, IM3Function i_function, ccstr_t i_errorMessage, ccstr_t i_file, u32 i_lineNum);

# if d_m3RecordBacktraces
void        PushBacktraceFrame         (IM3Runtime io_runtime, pc_t i_pc);
void        FillBacktraceFunctionInfo  (IM3Runtime io_runtime, IM3Function i_function);
void        ClearBacktrace             (IM3Runtime io_runtime);
# endif

d_m3EndExternC

#endif // m3_core_h
