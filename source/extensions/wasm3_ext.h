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

#if defined(__cplusplus)
extern "C" {
#endif


//-------------------------------------------------------------------------------------------------------------------------------
//  modules
//-------------------------------------------------------------------------------------------------------------------------------

	// Creates an empty module.
    IM3Module        	m3_NewModule 	            (IM3Environment         i_environment);

#if defined(__cplusplus)
}
#endif

#endif // wasm3_h
