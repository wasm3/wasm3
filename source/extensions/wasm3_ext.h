//
//  Wasm3, high performance WebAssembly interpreter
//
//  Extensions
//
//  Copyright © 2019-2021 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#ifndef wasm3_ext_h
#define wasm3_ext_h

#include "wasm3.h"
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

//-------------------------------------------------------------------------------------------------------------------------------
//  API extensions
//-------------------------------------------------------------------------------------------------------------------------------
/*
    These extensions allow for unconventional uses of Wasm3 -- mainly dynamic modification of modules to inject new Wasm
    functions during runtime.
*/
//-------------------------------------------------------------------------------------------------------------------------------

// Creates an empty module.
IM3Module   m3_NewModule (IM3Environment i_environment);


// To append a new function, set io_functionIndex to negative. On return, the new function index will be set.
// To overwrite an existing function, set io_functionIndex to the desired element. i_signature must match the existing
// function signature.
// ** InjectFunction invalidates any existing IM3Function pointers
M3Result    m3_InjectFunction (IM3Module            i_module,
                               int32_t*             io_functionIndex,
                               const char* const    i_signature,
                               const uint8_t* const i_wasmBytes,            // i_wasmBytes is copied
                               bool                 i_doCompilation);


IM3Function m3_GetFunctionByIndex (IM3Module i_module,
                                   uint32_t  i_index);


// A module in i_target that exports a view of each shared memory i_source exports, under the
// same names, loaded into i_target. Other exports are not carried over. The module comes out
// unnamed: name it with m3_SetModuleName, as the modules that import its memory expect.
//
// The bytes are shared, not copied: a memory.grow in either runtime is seen by the other,
// and atomic accesses and wait/notify reach across.
//
// i_source may itself be such a view, and the new one then shares the same memory. Each
// runtime is still used by one thread at a time, and i_source must not be freed while this
// runs. Without d_m3HasThreads there is nothing to share and this reports an error.
M3Result    m3_ShareModule (IM3Runtime i_target, IM3Module i_source, IM3Module* o_proxy);

#if defined(__cplusplus)
}
#endif

#endif // wasm3_h
