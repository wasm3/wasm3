//
//  Wasm3, high performance WebAssembly interpreter
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#ifndef wasm3_h
#define wasm3_h

#define M3_VERSION_MAJOR 0
#define M3_VERSION_MINOR 4
#define M3_VERSION_REV   9
#define M3_VERSION       "0.4.9"

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef const char *    M3Result;

struct M3Environment;   typedef struct M3Environment *  IM3Environment;
struct M3Runtime;       typedef struct M3Runtime *      IM3Runtime;
struct M3Module;        typedef struct M3Module *       IM3Module;
struct M3Function;      typedef struct M3Function *     IM3Function;

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

// Constants
#define M3_BACKTRACE_TRUNCATED      (void*)(SIZE_MAX)

typedef enum M3ValueType
{
    c_m3Type_none   = 0,
    c_m3Type_i32    = 1,
    c_m3Type_i64    = 2,
    c_m3Type_f32    = 3,
    c_m3Type_f64    = 4,

    c_m3Type_unknown
} M3ValueType;


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
#   define d_m3ErrorConst(LABEL, STRING)        M3Result m3Err_##LABEL = { STRING };
# else
#   define d_m3ErrorConst(LABEL, STRING)        extern M3Result m3Err_##LABEL;
# endif

// -------------------------------------------------------------------------------------------------------------------------------

d_m3ErrorConst  (none,                          NULL)

// general errors
d_m3ErrorConst  (typeListOverflow,              "type list count exceeds 32 types")
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
d_m3ErrorConst  (tooManyMemorySections,         "Wasm MVP can only define one memory per module")

// link errors
d_m3ErrorConst  (moduleAlreadyLinked,           "attempting to bind module to multiple runtimes")
d_m3ErrorConst  (functionLookupFailed,          "function lookup failed")
d_m3ErrorConst  (functionImportMissing,         "missing imported function")

d_m3ErrorConst  (malformedFunctionSignature,    "malformed function signature")

// compilation errors
d_m3ErrorConst  (noCompiler,                    "no compiler found for opcode")
d_m3ErrorConst  (unknownOpcode,                 "unknown opcode")
d_m3ErrorConst  (functionStackOverflow,         "compiling function overran its stack height limit")
d_m3ErrorConst  (functionStackUnderrun,         "compiling function underran the stack")
d_m3ErrorConst  (mallocFailedCodePage,          "memory allocation failed when acquiring a new M3 code page")
d_m3ErrorConst  (settingImmutableGlobal,        "attempting to set an immutable global")

// runtime errors
d_m3ErrorConst  (missingCompiledCode,           "function is missing compiled m3 code")
d_m3ErrorConst  (wasmMemoryOverflow,            "runtime ran out of memory")
d_m3ErrorConst  (globalMemoryNotAllocated,      "global memory is missing from a module")
d_m3ErrorConst  (globaIndexOutOfBounds,         "global index is too large")
d_m3ErrorConst  (argumentCountMismatch,         "argument count mismatch")

// traps
d_m3ErrorConst  (trapOutOfBoundsMemoryAccess,   "[trap] out of bounds memory access")
d_m3ErrorConst  (trapDivisionByZero,            "[trap] integer divide by zero")
d_m3ErrorConst  (trapIntegerOverflow,           "[trap] integer overflow")
d_m3ErrorConst  (trapIntegerConversion,         "[trap] invalid conversion to integer")
d_m3ErrorConst  (trapIndirectCallTypeMismatch,  "[trap] indirect call type mismatch")
d_m3ErrorConst  (trapTableIndexOutOfRange,      "[trap] undefined element")
d_m3ErrorConst  (trapTableElementIsNull,        "[trap] null table element")
d_m3ErrorConst  (trapExit,                      "[trap] program called exit")
d_m3ErrorConst  (trapAbort,                     "[trap] program called abort")
d_m3ErrorConst  (trapUnreachable,               "[trap] unreachable executed")
d_m3ErrorConst  (trapStackOverflow,             "[trap] stack overflow")


//-------------------------------------------------------------------------------------------------------------------------------
//  configuration, can be found in m3_config.h, m3_config_platforms.h, m3_core.h)
//-------------------------------------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------------------------------------
//  global environment than can host multiple runtimes
//-------------------------------------------------------------------------------------------------------------------------------
    IM3Environment      m3_NewEnvironment           (void);

    void                m3_FreeEnvironment          (IM3Environment i_environment);

//-------------------------------------------------------------------------------------------------------------------------------
//  execution context
//-------------------------------------------------------------------------------------------------------------------------------

    IM3Runtime          m3_NewRuntime               (IM3Environment         io_environment,
                                                     uint32_t               i_stackSizeInBytes,
                                                     void *                 i_userdata);

    void                m3_FreeRuntime              (IM3Runtime             i_runtime);

    uint8_t *           m3_GetMemory                (IM3Runtime             i_runtime,
                                                     uint32_t *             o_memorySizeInBytes,
                                                     uint32_t               i_memoryIndex);

    void *              m3_GetUserData              (IM3Runtime             i_runtime);

    // Wasm currently only supports one memory region. i_memoryIndex should be zero.

//-------------------------------------------------------------------------------------------------------------------------------
//  modules
//-------------------------------------------------------------------------------------------------------------------------------

    M3Result            m3_ParseModule              (IM3Environment         i_environment,
                                                     IM3Module *            o_module,
                                                     const uint8_t * const  i_wasmBytes,
                                                     uint32_t               i_numWasmBytes);
    // i_wasmBytes data must be persistent during the lifetime of the module

    void                m3_FreeModule               (IM3Module i_module);
    //  Only unloaded modules need to be freed

    M3Result            m3_LoadModule               (IM3Runtime io_runtime,  IM3Module io_module);
    //  LoadModule transfers ownership of a module to the runtime. Do not free modules once successfully imported into the runtime

    // Calling m3_RunStart is optional
    M3Result            m3_RunStart                 (IM3Module i_module);

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

    const char*         m3_GetModuleName            (IM3Module i_module);
    IM3Runtime          m3_GetModuleRuntime         (IM3Module i_module);

//-------------------------------------------------------------------------------------------------------------------------------
//  functions
//-------------------------------------------------------------------------------------------------------------------------------
    M3Result            m3_Yield                    (void);

    M3Result            m3_FindFunction             (IM3Function *          o_function,
                                                     IM3Runtime             i_runtime,
                                                     const char * const     i_functionName);

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

    // IM3Functions are valid during the lifetime of the originating runtime

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

#if defined(__cplusplus)
}
#endif

#endif // wasm3_h
