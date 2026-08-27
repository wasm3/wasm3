//
//  Wasm3, high performance WebAssembly interpreter
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#ifndef wasm3_h
#define wasm3_h

#define M3_VERSION_MAJOR 0
#define M3_VERSION_MINOR 9
#define M3_VERSION_REV   1
#define M3_VERSION       "0.9.1-beta.1"

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>

#include "wasm3_defs.h"

// Constants
#define M3_BACKTRACE_TRUNCATED      (IM3BacktraceFrame)(SIZE_MAX)

#if defined(__cplusplus)
extern "C" {
#endif

typedef const char *    M3Result;

struct M3Environment;   typedef struct M3Environment *  IM3Environment;
struct M3Runtime;       typedef struct M3Runtime *      IM3Runtime;
struct M3Module;        typedef struct M3Module *       IM3Module;
struct M3Function;      typedef struct M3Function *     IM3Function;
struct M3Global;        typedef struct M3Global *       IM3Global;

typedef struct M3ErrorInfo
{
    M3Result        result;

    IM3Runtime      runtime;
    IM3Module       module;
    IM3Function     function;

    const char *    file;
    uint32_t        line;

    const char *    message;
} M3ErrorInfo;

typedef struct M3BacktraceFrame
{
    uint32_t                     moduleOffset;
    IM3Function                  function;

    struct M3BacktraceFrame *    next;
}
M3BacktraceFrame, * IM3BacktraceFrame;

typedef struct M3BacktraceInfo
{
    IM3BacktraceFrame      frames;
    IM3BacktraceFrame      lastFrame;    // can be M3_BACKTRACE_TRUNCATED
}
M3BacktraceInfo, * IM3BacktraceInfo;


typedef enum M3ValueType
{
    c_m3Type_none   = 0,
    c_m3Type_i32    = 1,
    c_m3Type_i64    = 2,
    c_m3Type_f32    = 3,
    c_m3Type_f64    = 4,

    // Opaque 16-byte slot used purely so wasm3 can PARSE modules
    // whose function signatures or local-variable declarations
    // mention v128 (the SIMD value type, wasm-encoded as 0x7B).
    // Actual v128 OPCODES still error at compile-time with
    // m3Err_unknownOpcode - we only avoid the parse-time
    // m3Err_invalidTypeId rejection. LLVM's auto-vectorizer emits
    // unused v128 locals into many `+simd128` modules even when no
    // SIMD op executes; without this slot wasm3 rejects every such
    // module before it ever sees a function body.
    c_m3Type_v128   = 5,

    // Reference values are opaque pointer-sized words and null is always 0.
    // A funcref holds an IM3Function; an externref holds a host-defined handle,
    // which the host is free to encode however it likes as long as 0 means null.
    c_m3Type_funcref    = 6,
    c_m3Type_externref  = 7,

    // A caught exception, from the exception handling proposal. Holds a pointer
    // to a runtime-owned exception object, or 0 for the null reference. The
    // object belongs to the runtime and stays alive until the outermost
    // m3_Call() that produced it returns - a host that stashes one past that
    // point is holding a dangling reference.
    c_m3Type_exnref     = 8,

    // the number of concrete value types
    c_m3Type_count,

    // "not a valid type" - returned where a lookup or a mapping failed.
    c_m3Type_unknown = c_m3Type_count
} M3ValueType;

typedef struct M3TaggedValue
{
    M3ValueType type;
    union M3ValueUnion
    {
        uint32_t    i32;
        uint64_t    i64;
        float       f32;
        double      f64;
        const void* ref;        // funcref / externref; NULL is the null reference
    } value;
}
M3TaggedValue, * IM3TaggedValue;

typedef struct M3ImportInfo
{
    const char *    moduleUtf8;
    const char *    fieldUtf8;
}
M3ImportInfo, * IM3ImportInfo;


typedef struct M3ImportContext
{
    void *          userdata;
    IM3Function     function;
}
M3ImportContext, * IM3ImportContext;

// -------------------------------------------------------------------------------------------------------------------------------
//  error codes
// -------------------------------------------------------------------------------------------------------------------------------

# if defined(M3_IMPLEMENT_ERROR_STRINGS)
#   if defined(__cplusplus)
#     define d_m3ErrorConst(LABEL, STRING)      extern const M3Result m3Err_##LABEL = { STRING };
#   else
#     define d_m3ErrorConst(LABEL, STRING)      const M3Result m3Err_##LABEL = { STRING };
#   endif
# else
#   define d_m3ErrorConst(LABEL, STRING)        extern const M3Result m3Err_##LABEL;
# endif

// -------------------------------------------------------------------------------------------------------------------------------

d_m3ErrorConst  (none,                          NULL)

// general errors
d_m3ErrorConst  (mallocFailed,                  "memory allocation failed")

// parse errors
d_m3ErrorConst  (incompatibleWasmVersion,       "incompatible Wasm binary version")
d_m3ErrorConst  (wasmMalformed,                 "malformed Wasm binary")
d_m3ErrorConst  (misorderedWasmSection,         "out of order Wasm section")
d_m3ErrorConst  (wasmUnderrun,                  "underrun while parsing Wasm binary")
d_m3ErrorConst  (wasmOverrun,                   "overrun while parsing Wasm binary")
d_m3ErrorConst  (wasmMissingInitExpr,           "missing init_expr in Wasm binary")
d_m3ErrorConst  (lebOverflow,                   "LEB encoded value overflow")
d_m3ErrorConst  (missingUTF8,                   "invalid length UTF-8 string")
d_m3ErrorConst  (wasmSectionUnderrun,           "section underrun while parsing Wasm binary")
d_m3ErrorConst  (wasmSectionOverrun,            "section overrun while parsing Wasm binary")
d_m3ErrorConst  (invalidTypeId,                 "unknown value_type")
d_m3ErrorConst  (tooManyMemorySections,         "only one memory per module is supported")
d_m3ErrorConst  (tooManyArgsRets,               "too many arguments or return values")

// link errors
d_m3ErrorConst  (moduleNotLinked,               "attempting to use module that is not loaded")
d_m3ErrorConst  (moduleAlreadyLinked,           "attempting to bind module to multiple runtimes")
d_m3ErrorConst  (functionLookupFailed,          "function lookup failed")
d_m3ErrorConst  (functionImportMissing,         "missing imported function")
d_m3ErrorConst  (unknownImport,                 "unknown import")
d_m3ErrorConst  (incompatibleImportType,        "incompatible import type")

d_m3ErrorConst  (malformedFunctionSignature,    "malformed function signature")

// compilation errors
d_m3ErrorConst  (noCompiler,                    "no compiler found for opcode")
d_m3ErrorConst  (unknownOpcode,                 "unknown opcode")
d_m3ErrorConst  (restrictedOpcode,              "restricted opcode")
d_m3ErrorConst  (functionStackOverflow,         "compiling function overran its stack height limit")
d_m3ErrorConst  (functionStackUnderrun,         "compiling function underran the stack")
d_m3ErrorConst  (mallocFailedCodePage,          "memory allocation failed when acquiring a new M3 code page")
d_m3ErrorConst  (settingImmutableGlobal,        "attempting to set an immutable global")
d_m3ErrorConst  (typeMismatch,                  "incorrect type on stack")
d_m3ErrorConst  (typeCountMismatch,             "incorrect value count on stack")

// validation errors. The wording follows the spec's own assert_invalid failure
d_m3ErrorConst  (unknownType,                   "unknown type")
d_m3ErrorConst  (unknownLabel,                  "unknown label")
d_m3ErrorConst  (unknownLocal,                  "unknown local")
d_m3ErrorConst  (unknownGlobal,                 "unknown global")
d_m3ErrorConst  (unknownFunction,               "unknown function")
d_m3ErrorConst  (unknownTable,                  "unknown table")
d_m3ErrorConst  (unknownTag,                    "unknown tag")
d_m3ErrorConst  (unknownMemory,                 "unknown memory")
d_m3ErrorConst  (unknownDataSegment,            "unknown data segment")
d_m3ErrorConst  (unknownElemSegment,            "unknown elem segment")
d_m3ErrorConst  (dataCountRequired,             "data count section required")
d_m3ErrorConst  (invalidAlignment,              "alignment must not be larger than natural")
d_m3ErrorConst  (undeclaredFuncRef,             "undeclared function reference")

// runtime errors
d_m3ErrorConst  (missingCompiledCode,           "function is missing compiled m3 code")
d_m3ErrorConst  (wasmMemoryOverflow,            "runtime ran out of memory")
d_m3ErrorConst  (globalMemoryNotAllocated,      "global memory is missing from a module")
d_m3ErrorConst  (globaIndexOutOfBounds,         "global index is too large")
d_m3ErrorConst  (argumentCountMismatch,         "argument count mismatch")
d_m3ErrorConst  (argumentTypeMismatch,          "argument type mismatch")
d_m3ErrorConst  (globalLookupFailed,            "global lookup failed")
d_m3ErrorConst  (globalTypeMismatch,            "global type mismatch")
d_m3ErrorConst  (globalNotMutable,              "global is not mutable")

// traps
d_m3ErrorConst  (trapOutOfBoundsMemoryAccess,   "[trap] out of bounds memory access")
d_m3ErrorConst  (trapDivisionByZero,            "[trap] integer divide by zero")
d_m3ErrorConst  (trapIntegerOverflow,           "[trap] integer overflow")
d_m3ErrorConst  (trapIntegerConversion,         "[trap] invalid conversion to integer")
d_m3ErrorConst  (trapIndirectCallTypeMismatch,  "[trap] indirect call type mismatch")
d_m3ErrorConst  (trapTableIndexOutOfRange,      "[trap] undefined element")
d_m3ErrorConst  (trapTableElementIsNull,        "[trap] uninitialized element")
d_m3ErrorConst  (trapNullReference,             "[trap] null reference")
d_m3ErrorConst  (trapNullFunctionRef,           "[trap] null function reference")
// call_indirect past the end of the table is "undefined element"; the table
// access instructions report an out of bounds access instead
d_m3ErrorConst  (trapTableOutOfBounds,          "[trap] out of bounds table access")
d_m3ErrorConst  (trapExit,                      "[trap] program called exit")
d_m3ErrorConst  (trapAbort,                     "[trap] program called abort")
d_m3ErrorConst  (trapUnreachable,               "[trap] unreachable")
d_m3ErrorConst  (trapStackOverflow,             "[trap] stack overflow")
d_m3ErrorConst  (trapUncaughtException,         "[trap] uncaught exception")

// Internal: the marker an in-flight exception rides back up the native stack
// on. Never escapes m3_Call - the outermost RunCodeChecked turns it into
// m3Err_trapUncaughtException.
d_m3ErrorConst  (pendingException,              "[internal] exception in flight")


//-------------------------------------------------------------------------------------------------------------------------------
//  configuration, can be found in m3_config.h, m3_config_platforms.h, m3_core.h)
//-------------------------------------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------------------------------------
//  global environment than can host multiple runtimes
//-------------------------------------------------------------------------------------------------------------------------------
    IM3Environment      m3_NewEnvironment           (void);

    void                m3_FreeEnvironment          (IM3Environment i_environment);

    typedef M3Result (* M3SectionHandler) (IM3Module i_module, const char* name, const uint8_t * start, const uint8_t * end);

    void                m3_SetCustomSectionHandler  (IM3Environment i_environment,    M3SectionHandler i_handler);


//-------------------------------------------------------------------------------------------------------------------------------
//  execution context
//-------------------------------------------------------------------------------------------------------------------------------

    IM3Runtime          m3_NewRuntime               (IM3Environment         io_environment,
                                                     uint32_t               i_stackSizeInBytes,
                                                     void *                 i_userdata);

    void                m3_FreeRuntime              (IM3Runtime             i_runtime);

    // A memory belongs to the module that declares it, so these take the module
    // rather than the runtime - a runtime can hold several modules, each with
    // its own memories. Returns NULL when the module has no memory at that
    // index; o_memorySizeInBytes is set either way.
    //
    // Sizes are size_t, not uint32_t: a linear memory may be a full 4 GiB, which
    // is one byte too many to count in 32 bits.
    uint8_t *           m3_GetMemory                (IM3Module              i_module,
                                                     size_t *               o_memorySizeInBytes,
                                                     uint32_t               i_memoryIndex);

    size_t              m3_GetMemorySize            (IM3Module              i_module,
                                                     uint32_t               i_memoryIndex);

    // Size of the memory a pointer addresses into - specifically the _mem a raw
    // function is handed, which is the memory of whichever module is calling,
    // and need not be any particular module's. Used by m3ApiCheckMem.
    size_t              m3_GetMemorySizeAt          (const void *           i_memory);

    void *              m3_GetUserData              (IM3Runtime             i_runtime);


//-------------------------------------------------------------------------------------------------------------------------------
//  modules
//-------------------------------------------------------------------------------------------------------------------------------

    // i_wasmBytes data must be persistent during the lifetime of the module
    M3Result            m3_ParseModule              (IM3Environment         i_environment,
                                                     IM3Module *            o_module,
                                                     const uint8_t * const  i_wasmBytes,
                                                     uint32_t               i_numWasmBytes);

    // Only a module that was never handed to m3_LoadModule needs to be freed.
    void                m3_FreeModule               (IM3Module i_module);

    //  Transfers ownership of the module to the runtime - whether or not it
    //  succeeds. A failed instantiation can already have written this module's
    //  functions into a table another module owns, and the spec keeps whatever
    //  it managed to do, so those entries stay callable; the module has to
    //  outlive the failure for them not to dangle. m3_FreeRuntime releases it.
    //  Do not call m3_FreeModule on a module after passing it here.
    M3Result            m3_LoadModule               (IM3Runtime io_runtime,  IM3Module io_module);

    // Optional, compiles all functions in the module
    M3Result            m3_CompileModule            (IM3Module io_module);

    // Calling m3_RunStart is optional
    M3Result            m3_RunStart                 (IM3Module i_module);

    // Arguments and return values are passed in and out through the stack pointer _sp.
    // Placeholder return value slots are first and arguments after. So, the first argument is at _sp [numReturns]
    // Return values should be written into _sp [0] to _sp [num_returns - 1]
    typedef const void * (* M3RawCall) (IM3Runtime runtime, IM3ImportContext _ctx, uint64_t * _sp, void * _mem);

    M3Result            m3_LinkRawFunction          (IM3Module              io_module,
                                                     const char * const     i_moduleName,
                                                     const char * const     i_functionName,
                                                     const char * const     i_signature,
                                                     M3RawCall              i_function);

    M3Result            m3_LinkRawFunctionEx        (IM3Module              io_module,
                                                     const char * const     i_moduleName,
                                                     const char * const     i_functionName,
                                                     const char * const     i_signature,
                                                     M3RawCall              i_function,
                                                     const void *           i_userdata);

    // supplies the value of an imported global, regardless of its mutability
    M3Result            m3_LinkGlobal               (IM3Module              io_module,
                                                     const char * const     i_moduleName,
                                                     const char * const     i_globalName,
                                                     const IM3TaggedValue   i_value);

    const char*         m3_GetModuleName            (IM3Module i_module);
    void                m3_SetModuleName            (IM3Module i_module, const char* name);
    IM3Runtime          m3_GetModuleRuntime         (IM3Module i_module);

    // The module registered under i_moduleName, or NULL. Most recently loaded
    // first, so a name registered twice names the newer module.
    IM3Module           m3_FindModule               (IM3Runtime             i_runtime,
                                                     const char * const     i_moduleName);

//-------------------------------------------------------------------------------------------------------------------------------
//  globals
//-------------------------------------------------------------------------------------------------------------------------------
    IM3Global           m3_FindGlobal               (IM3Module              io_module,
                                                     const char * const     i_globalName);

    M3Result            m3_GetGlobal                (IM3Global              i_global,
                                                     IM3TaggedValue         o_value);

    M3Result            m3_SetGlobal                (IM3Global              i_global,
                                                     const IM3TaggedValue   i_value);

    M3ValueType         m3_GetGlobalType            (IM3Global              i_global);

//-------------------------------------------------------------------------------------------------------------------------------
//  functions
//-------------------------------------------------------------------------------------------------------------------------------
    M3Result            m3_Yield                    (void);

    // o_function is valid during the lifetime of the originating runtime.
    // m3_FindFunction searches every module loaded into the runtime, most
    // recently loaded first; m3_FindFunctionIn searches just the one, which is
    // what naming a module's export means.
    M3Result            m3_FindFunction             (IM3Function *          o_function,
                                                     IM3Runtime             i_runtime,
                                                     const char * const     i_functionName);
    M3Result            m3_FindFunctionIn           (IM3Function *          o_function,
                                                     IM3Module              i_module,
                                                     const char * const     i_functionName);
    M3Result            m3_GetTableFunction         (IM3Function *          o_function,
                                                     IM3Module              i_module,
                                                     uint32_t               i_index);

    uint32_t            m3_GetArgCount              (IM3Function i_function);
    uint32_t            m3_GetRetCount              (IM3Function i_function);
    M3ValueType         m3_GetArgType               (IM3Function i_function, uint32_t i_index);
    M3ValueType         m3_GetRetType               (IM3Function i_function, uint32_t i_index);

    M3Result            m3_CallV                    (IM3Function i_function, ...);
    M3Result            m3_CallVL                   (IM3Function i_function, va_list i_args);
    M3Result            m3_Call                     (IM3Function i_function, uint32_t i_argc, const void * i_argptrs[]);
    M3Result            m3_CallArgv                 (IM3Function i_function, uint32_t i_argc, const char * i_argv[]);

    M3Result            m3_GetResultsV              (IM3Function i_function, ...);
    M3Result            m3_GetResultsVL             (IM3Function i_function, va_list o_rets);
    M3Result            m3_GetResults               (IM3Function i_function, uint32_t i_retc, const void * o_retptrs[]);


    void                m3_GetErrorInfo             (IM3Runtime i_runtime, M3ErrorInfo* o_info);
    void                m3_ResetErrorInfo           (IM3Runtime i_runtime);

    const char*         m3_GetFunctionName          (IM3Function i_function);
    IM3Module           m3_GetFunctionModule        (IM3Function i_function);

//-------------------------------------------------------------------------------------------------------------------------------
//  debug info
//-------------------------------------------------------------------------------------------------------------------------------

    void                m3_PrintRuntimeInfo         (IM3Runtime i_runtime);
    void                m3_PrintM3Info              (void);
    void                m3_PrintProfilerInfo        (void);

    // The runtime owns the backtrace, do not free the backtrace you obtain. Returns NULL if there's no backtrace.
    IM3BacktraceInfo    m3_GetBacktrace             (IM3Runtime i_runtime);

//-------------------------------------------------------------------------------------------------------------------------------
//  raw function definition helpers
//-------------------------------------------------------------------------------------------------------------------------------

# define m3ApiOffsetToPtr(offset)   (void*)((uint8_t*)_mem + (uint32_t)(offset))
# define m3ApiPtrToOffset(ptr)      (uint32_t)((uint8_t*)ptr - (uint8_t*)_mem)

# define m3ApiReturnType(TYPE)                 TYPE* raw_return = ((TYPE*) (_sp++));
# define m3ApiMultiValueReturnType(TYPE, NAME) TYPE* NAME = ((TYPE*) (_sp++));
# define m3ApiGetArg(TYPE, NAME)               TYPE NAME = \
    (sizeof(TYPE) >= sizeof(uint32_t)) ? \
    (*((TYPE *)(_sp++))) : \
    ((TYPE)(uintptr_t)(*((uint32_t *)(_sp++))));
# define m3ApiGetArgMem(TYPE, NAME)            TYPE NAME = (TYPE)m3ApiOffsetToPtr(* ((uint32_t *) (_sp++)));

# define m3ApiIsNullPtr(addr)       ((void*)(addr) <= _mem)

// Whether [addr, addr+len) lies inside the memory _mem points at. Written as
// two subtractions rather than base + size and addr + len, so that neither a
// memory occupying the top of the address space nor a wild length can carry the
// comparison past where it wraps. Its own block, so the locals go out of scope
// with it.
# define m3ApiCheckMem(addr, len)                                                   \
    {   uintptr_t _m3_base = (uintptr_t)(_mem);                                     \
        uintptr_t _m3_addr = (uintptr_t)(void*)(addr);                              \
        size_t    _m3_size = m3_GetMemorySizeAt(_mem);                              \
        if (M3_UNLIKELY(_m3_addr < _m3_base ||                                      \
                        (size_t)(_m3_addr - _m3_base) > _m3_size ||                 \
                        (uint64_t)(len) > (uint64_t)(_m3_size - (size_t)(_m3_addr - _m3_base)))) \
            m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);                           \
    }

# define m3ApiRawFunction(NAME)     const void * NAME (IM3Runtime runtime, IM3ImportContext _ctx, uint64_t * _sp, void * _mem)
# define m3ApiReturn(VALUE)                   { *raw_return = (VALUE); return m3Err_none;}
# define m3ApiMultiValueReturn(NAME, VALUE)   { *NAME = (VALUE); }
# define m3ApiTrap(VALUE)                     { return VALUE; }
# define m3ApiSuccess()                       { return m3Err_none; }

# if defined(M3_BIG_ENDIAN)
#  define m3ApiReadMem8(ptr)         (* (uint8_t *)(ptr))
#  define m3ApiReadMem16(ptr)        m3_bswap16((* (uint16_t *)(ptr)))
#  define m3ApiReadMem32(ptr)        m3_bswap32((* (uint32_t *)(ptr)))
#  define m3ApiReadMem64(ptr)        m3_bswap64((* (uint64_t *)(ptr)))
#  define m3ApiWriteMem8(ptr, val)   { * (uint8_t  *)(ptr)  = (val); }
#  define m3ApiWriteMem16(ptr, val)  { * (uint16_t *)(ptr) = m3_bswap16((val)); }
#  define m3ApiWriteMem32(ptr, val)  { * (uint32_t *)(ptr) = m3_bswap32((val)); }
#  define m3ApiWriteMem64(ptr, val)  { * (uint64_t *)(ptr) = m3_bswap64((val)); }
# else
#  define m3ApiReadMem8(ptr)         (* (uint8_t *)(ptr))
#  define m3ApiReadMem16(ptr)        (* (uint16_t *)(ptr))
#  define m3ApiReadMem32(ptr)        (* (uint32_t *)(ptr))
#  define m3ApiReadMem64(ptr)        (* (uint64_t *)(ptr))
#  define m3ApiWriteMem8(ptr, val)   { * (uint8_t  *)(ptr) = (val); }
#  define m3ApiWriteMem16(ptr, val)  { * (uint16_t *)(ptr) = (val); }
#  define m3ApiWriteMem32(ptr, val)  { * (uint32_t *)(ptr) = (val); }
#  define m3ApiWriteMem64(ptr, val)  { * (uint64_t *)(ptr) = (val); }
# endif

#if defined(__cplusplus)
}
#endif

#endif // wasm3_h
