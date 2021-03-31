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

	// Creates an empty module.
    IM3Module        	w3_NewModule 	            (IM3Environment         i_environment);


	// To append a new function, set io_functionIndex to negative. On return, the new function index will be set.
	// To overwrite an existing function, set io_functionIndex to the desired element. i_signature must match the existing
	// function signature.
	M3Result            w3_InjectFunction           (IM3Module              i_module,
													 int32_t *              io_functionIndex,
													 const char * const     i_signature,
													 const uint8_t * const  i_wasmBytes,			// i_wasmBytes is copied
													 uint32_t               i_numWasmBytes,
													 bool					i_doCompilation);


#if defined(__cplusplus)
}
#endif

#endif // wasm3_h
