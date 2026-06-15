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

#include "m3_exec_defs.h"

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
    IM3Module           w3x_NewModule         	    (IM3Environment         i_environment);

    // ReserveFunctions must be called prior to LoadModule.  This reserves extra empty function slots that InjectFunction
    // can rely upon.
    M3Result            w3x_ReserveFunctions        (IM3Module              i_module,
                                                     uint32_t               i_numFunctions);

	uint32_t			w3x_GetNumFunctions			(IM3Module				i_module);

    // To append a new function, set io_functionIndex to negative. On return, the new function index will be set.
    // To overwrite an existing function, set io_functionIndex to the desired element. i_signature must match the existing
    // function signature.  TODO: failure result is?
    M3Result            w3x_InjectFunction          (IM3Module              i_module,
                                                     int32_t *              io_functionIndex,
													 const char * const     i_name,
                                                     const char * const     i_signature,
                                                     const uint8_t * const  i_wasmBytes,            // i_wasmBytes are copied
													 const uint32_t			i_numWasmBytes,
                                                     bool                   i_doCompilation);

    M3Result            w3x_AllocateFunction        (IM3Module              i_module,
                                                     int32_t *              o_functionIndex,
													 const char * const     i_name,					// only assigned if (d_m3LogCompile == 1)
                                                     const char * const     i_signature);


	M3Result			w3x_AttachFunctionCode	    (IM3Module				i_module,
													 int32_t				i_functionIndex,
                                                     const uint8_t * const  i_wasmBytes,
													 const uint32_t			i_numWasmBytes,
                                                     bool                   i_doCompilation);


    M3Result            w3x_AddFunctionToTable      (IM3Function            i_function,
                                                     uint32_t *             o_elementIndex,
                                                     uint32_t               i_tableIndex);          // i_tableIndex must be zero


    IM3Function         m3_GetFunctionByIndex       (IM3Module              i_module,
                                                     uint32_t               i_index);

    M3Result            m3_GetFunctionIndex         (IM3Function            i_function,
                                                     uint32_t *             o_index);

    M3Result            w3x_GetDataSegmentInfo      (IM3Module              i_module,
													 uint32_t *				o_offset,
													 uint32_t *				o_size,
                                                     uint32_t               i_dataSegmentIndex);


	// Serializes a live module back into a valid Wasm binary.  Data segments are snapshotted from the
	// runtime's linear memory and the code section is rebuilt from each function's wasm, so functions
	// injected after load are captured. Globals modified through the m3_SetGlobal api will also be captured
	// and the original parsed init_expr will be ignored.
	// On success *o_wasmBytes is a heap buffer owned by the caller (free with m3_Free).
	M3Result            w3x_GenerateModuleWasm      (IM3Module              	i_module,				// module must be loaded into a runtime
													 uint8_t **             	o_wasmBytes,
													 uint32_t *             	o_numWasmBytes);

//-------------------------------------------------------------------------------------------------------------------------------

    M3Result            m3_RegisterCustomOpcode     (IM3Module              i_module,
                                                     uint16_t               i_opcode,
                                                     uint8_t                i_numImmediates,
                                                     IM3Operation           i_operation);

//-------------------------------------------------------------------------------------------------------------------------------


#if defined(__cplusplus)
}
#endif

#endif // wasm3_h
