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
#define M3_VERSION_REV   5
#define M3_VERSION       "0.4.5"

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "m3_error.h"

#if defined(__cplusplus)
extern "C" {
#endif

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
}
M3ErrorInfo;


typedef struct M3StackInfo
{
    void *          startAddr;
    int32_t         stackSize;
}
M3StackInfo;


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

    c_m3Type_runtime
};


typedef struct M3ImportInfo
{
    const char *    moduleUtf8;
    const char *    fieldUtf8;

//  unsigned char   type;
}
M3ImportInfo;

typedef M3ImportInfo * IM3ImportInfo;


//-------------------------------------------------------------------------------------------------------------------------------
//  configuration, can be found in m3_config.h, m3_config_platforms.h, m3_core.h)
//-------------------------------------------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------------------------------------------
//  initialization
//-------------------------------------------------------------------------------------------------------------------------------

    // not yet implemented
//  M3StackInfo         m3_GetNativeStackInfo       (int32_t                i_stackSize);
    // GetNativeStackInfo should be called at the start of main() or, if runtimes are used in a thread,
    // at the start of the thread start function.

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
                                                     M3StackInfo *          i_nativeStackInfo);     // i_nativeStackInfo can be NULL

    void                m3_FreeRuntime              (IM3Runtime             i_runtime);
    
    const uint8_t *     m3_GetMemory                (IM3Runtime             i_runtime,
                                                     uint32_t *             o_memorySizeInBytes,
                                                     uint32_t               i_memoryIndex);
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

    typedef const void * (* M3RawCall) (IM3Runtime runtime, uint64_t * _sp, void * _mem);

    M3Result            m3_LinkRawFunction          (IM3Module              io_module,
                                                     const char * const     i_moduleName,
                                                     const char * const     i_functionName,
                                                     const char * const     i_signature,
                                                     M3RawCall              i_function);

//-------------------------------------------------------------------------------------------------------------------------------
//  functions
//-------------------------------------------------------------------------------------------------------------------------------

    M3Result            m3_FindFunction             (IM3Function *          o_function,
                                                     IM3Runtime             i_runtime,
                                                     const char * const     i_functionName);

    M3Result            m3_Call                     (IM3Function i_function);
    M3Result            m3_CallWithArgs             (IM3Function i_function, uint32_t i_argc, const char * const * i_argv);

    // IM3Functions are valid during the lifetime of the originating runtime

    void                m3_GetErrorInfo             (IM3Runtime i_runtime, M3ErrorInfo* info);
    void                m3_ResetErrorInfo           (IM3Runtime i_runtime);

//-------------------------------------------------------------------------------------------------------------------------------
//  debug info
//-------------------------------------------------------------------------------------------------------------------------------

    void                m3_PrintRuntimeInfo         (IM3Runtime i_runtime);
    void                m3_PrintM3Info              (void);
    void                m3_PrintProfilerInfo        (void);

#if defined(__cplusplus)
}
#endif

#endif // wasm3_h
