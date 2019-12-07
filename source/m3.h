//
//  m3.h
//
//  M3 / Massey Meta Machine: a WebAssembly interpreter
//
//  Created by Steven Massey on 4/21/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

/*
    NOTES:
        - define d_m3LogOutput=1 to see printf log info

    FIX:
        - function types need to move to the runtime structure so that all modules can share types
            then type equality can be a simple pointer compare for indirect call checks

    TODO:
        - assumes little-endian CPU
        - needs work for a 32-bit architecture
            - e.g. m3 code stream should be 32-bit aligned, but still needs to handle 64-bit constants

    POSSIBLE FUTURE FEATURES:
        - segmented stack
        - M3 stack that lives on the C stack (this might be useful in a memory constrained environment)
        - i32, f32 could occupy 4 bytes on M3 stack
        - support of tail calls wasm extension
        - WASI support
 */


#ifndef m3_h
#define m3_h

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef const char *    M3Result;

struct M3Runtime;       typedef struct M3Runtime *      IM3Runtime;
struct M3Module;        typedef struct M3Module *       IM3Module;
struct M3Function;      typedef struct M3Function *     IM3Function;


typedef struct M3ErrorInfo
{
    M3Result        result;

    IM3Runtime      runtime;
    IM3Module       module;
    IM3Function     function;

    // compilation constants
    const char *    file;
    uint32_t        line;

    char            message         [256];
}
M3ErrorInfo;



enum // EWaTypes
{
    c_m3Type_none   = 0,
    c_m3Type_i32    = 1,
    c_m3Type_i64    = 2,
    c_m3Type_f32    = 3,
    c_m3Type_f64    = 4,

    c_m3Type_void,
    c_m3Type_ptr,
    c_m3Type_trap,

    c_m3Type_module
};


typedef struct M3ImportInfo
{
    const char *    moduleUtf8;
    const char *    fieldUtf8;

//  unsigned char   type;
}
M3ImportInfo;

typedef M3ImportInfo * IM3ImportInfo;



// -------------------------------------------------------------------------------------------------------------------------------
//  error codes
// -------------------------------------------------------------------------------------------------------------------------------

# if defined(M3_IMPLEMENT_ERROR_STRINGS)
#   define d_m3ErrorConst(LABEL, STRING)        M3Result c_m3Err_##LABEL = { STRING };
# else
#   define d_m3ErrorConst(LABEL, STRING)        extern M3Result c_m3Err_##LABEL;
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
d_m3ErrorConst  (functionImportMissing,         "missing imported function");

// compilation errors
d_m3ErrorConst  (noCompiler,                    "no compiler found for opcode")
d_m3ErrorConst  (unknownOpcode,                 "unknown opcode")
d_m3ErrorConst  (functionStackOverflow,         "compiling function overrun its stack height limit")
d_m3ErrorConst  (functionStackUnderrun,         "compiling function underran the stack")
d_m3ErrorConst  (mallocFailedCodePage,          "memory allocation failed when acquiring a new M3 code page")
d_m3ErrorConst  (optimizerFailed,               "optimizer failed") // not a fatal error. a result,

// runtime errors
d_m3ErrorConst  (missingCompiledCode,           "function is missing compiled m3 code")
d_m3ErrorConst  (wasmMemoryOverflow,            "runtime ran out of memory")
d_m3ErrorConst  (globalMemoryNotAllocated,      "global memory is missing from a module")
d_m3ErrorConst  (globaIndexOutOfBounds,         "global index is too large")

// traps
d_m3ErrorConst  (trapOutOfBoundsMemoryAccess,   "[trap] out of bounds memory access")
d_m3ErrorConst  (trapDivisionByZero,            "[trap] integer divide by zero")
d_m3ErrorConst  (trapIntegerOverflow,           "[trap] integer overflow")
d_m3ErrorConst  (trapIntegerConversion,         "[trap] invalid conversion to integer")
d_m3ErrorConst  (trapIndirectCallTypeMismatch,  "[trap] indirect call type mismatch")
d_m3ErrorConst  (trapTableIndexOutOfRange,      "[trap] undefined element")
d_m3ErrorConst  (trapExit,                      "[trap] program called exit")
d_m3ErrorConst  (trapUnreachable,               "[trap] unreachable executed")


typedef void    (* M3Free)      (const void * i_data, void * i_ref);
typedef void    (* M3Importer)  (IM3ImportInfo io_import, IM3Module io_module, void * i_ref);
typedef int64_t (* M3Callback)  (IM3Function i_currentFunction, void * i_ref);


//-------------------------------------------------------------------------------------------------------------------------------
//  configuration                                                                                           (found in m3_core.h)
//-------------------------------------------------------------------------------------------------------------------------------
/*
        define                          default
        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   ~~~~~~~~~~  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        d_m3AlignWasmMemoryToPages      false       The WebAssembly spec defines a 64kB page size and memory size is always
                                                    quantized to pages. In a non-Javascript centric WA host this seems
                                                    unnecessary.
 */


//--------------------------------------------------------------------------------------------------------------------------------------------
//  "global" environment
//--------------------------------------------------------------------------------------------------------------------------------------------
//  IM3Environment      m3_NewEnvironment

//--------------------------------------------------------------------------------------------------------------------------------------------
//  execution context
//--------------------------------------------------------------------------------------------------------------------------------------------

    IM3Runtime          m3_NewRuntime               (uint32_t i_stackSizeInBytes);


    M3Result            m3_RegisterFunction         (IM3Runtime             io_runtime,
                                                     const char * const     i_functionName,
                                                     const char * const     i_signature,
                                                     const void * const     i_function /* , const void * const i_ref */);


    void                m3_FreeRuntime              (IM3Runtime i_runtime);

//  void                m3_SetImporter              (IM3Runtime i_runtime, M3Importer i_importHandler);
//  void                m3_SetTimeoutHandler        (IM3Runtime i_runtime, float i_periodInSeconds, M3Callback i_callback);

//--------------------------------------------------------------------------------------------------------------------------------------------
//  modules
//--------------------------------------------------------------------------------------------------------------------------------------------

    M3Result            m3_ParseModule              (IM3Module *            o_module,
                                                     const uint8_t * const  i_wasmBytes,
                                                     uint32_t               i_numWasmBytes
                             // M3Free              i_releaseHandler        // i_ref argument type provided to M3Free() handler is <IM3Module>
                             );
    //  If i_wasmReleaseHandler is provided, then i_wasmBytes must be persistent until the handler is invoked.
    //  If the handler is NULL, ParseModule will make a copy of i_wasmBytes and releases ownership of the pointer.
    //  if a result is return, * o_module will be set to NULL


    void                m3_FreeModule               (IM3Module i_module);
    //  Only unloaded modules need to be freed.


//  M3Result            m3_EnableOptimizer          (IM3Module io_module,  bool i_enable);

    M3Result            m3_LoadModule               (IM3Runtime io_runtime,  IM3Module io_module);
    //  LoadModule transfers ownership of a module to the runtime. Do not free modules once successfully imported into the runtime.


    M3Result            m3_LinkFunction             (IM3Module              io_module,
                                                     const char * const     i_functionName,
                                                     const char * const     i_signature,
                                                     const void * const     i_function /* , const void * const i_ref */);

    // signature is null terminated

//  M3Result            m3_SetGlobal

//--------------------------------------------------------------------------------------------------------------------------------------------
//  functions
//--------------------------------------------------------------------------------------------------------------------------------------------

    M3Result            m3_FindFunction             (IM3Function *          o_function,
                                                     IM3Runtime             i_runtime,
                                                     const char * const     i_functionName);

//  M3Result            m3_GetCFunction             (void ** o_cFunction,  IM3Runtime i_runtime,
//                                                   const char * const i_functionName, const char * const i_signature);

    M3Result            m3_Call                     (IM3Function i_function);
    M3Result            m3_CallWithArgs             (IM3Function i_function, uint32_t i_argc, const char * const * i_argv);
    M3Result            m3_CallMain                 (IM3Function i_function, uint32_t i_argc, const char * const * i_argv);

//  void * /* return */ m3_Call                     (IM3Function i_function, M3Result * o_result);

    // IM3Functions are valid during the lifetime of the originating runtime

    M3ErrorInfo         m3_GetErrorInfo             (IM3Runtime i_runtime);
    void                m3_IgnoreErrorInfo          (IM3Runtime i_runtime);

//--------------------------------------------------------------------------------------------------------------------------------------------
//  debug info
//--------------------------------------------------------------------------------------------------------------------------------------------

    void                m3_PrintRuntimeInfo         (IM3Runtime i_runtime);
    void                m3_PrintM3Info              (void);
    void                m3_PrintProfilerInfo        (void);

#if defined(__cplusplus)
}
#endif

#endif /* m3_h */
