//
//  m3_extensions.c
//
//  Created by Steven Massey on 3/30/21.
//  Copyright © 2021 Steven Massey. All rights reserved.
//

#include "wasm3_ext.h"

#include "m3_env.h"
#include "m3_bind.h"
#include "m3_exception.h"

IM3Module  w3x_NewModule  (IM3Environment         i_environment)
{
	return Module_NewModule (i_environment);
}


M3Result  w3x_ReserveFunctions  (IM3Module                i_module,
								 uint32_t                 i_numFunctions)
{
    M3Result result = m3Err_none;

    if (i_module)
    {                                                       d_m3Assert (i_module->table0Size == 0);
        if (i_module->table0Size == 0)
        {
_           (Module_PreallocFunctions (i_module, i_module->numFunctions + i_numFunctions));
        }
        else _throw ("ReserveFunctions must come before LoadModule");
    }
    else _throw (m3Err_nullArgument);

    _catch:
    return result;
}


i32  Module_HasFuncType  (IM3Module i_module, IM3FuncType i_funcType)
{
    if (i_module->funcTypes)
    {
        for (u32 i = 0; i < i_module->numFuncTypes; ++i)
        {
            if (AreFuncTypesEqual (i_module->funcTypes [i], i_funcType))
                return i;
        }
    }

    return -1;
}


M3Result  w3x_InjectFunction  (IM3Module                 i_module,
							   int32_t *                 io_functionIndex,
							   const char * const        i_signature,
							   const uint8_t * const     i_wasmBytes,
							   const uint32_t			 i_numWasmBytes,
							   bool                      i_doCompilation)
{
                                                                        d_m3Assert (io_functionIndex);
    IM3FuncType ftype = NULL;

    _try {
	
    if (not i_module)
        _throw (m3Err_nullArgument);

    IM3Function function = NULL;
_   (SignatureToFuncType (& ftype, i_signature));

    i32 index = * io_functionIndex;

    bytes_t bytes = i_wasmBytes;
    bytes_t end = i_wasmBytes + i_numWasmBytes;

    u32 size;
_   (ReadLEB_u32 (& size, & bytes, end));
	bytes_t calculatedEnd = bytes + size;								_throwif (m3Err_wasmOverrun, calculatedEnd > end);
																		_throwif (m3Err_wasmUnderrun, calculatedEnd < end);
    end = calculatedEnd;

    if (index >= 0)
    {
        _throwif ("function index out of bounds", index >= i_module->numFunctions);

        function = & i_module->functions [index];

        if (not AreFuncTypesEqual (ftype, function->funcType))
            _throw ("function type mismatch");
    }
    else
    {
        i32 funcTypeIndex = Module_HasFuncType (i_module, ftype);
        if (funcTypeIndex < 0)
        {
            // add new slot to function type table in the module
            funcTypeIndex = i_module->numFuncTypes++;
            i_module->funcTypes = m3_ReallocArray (IM3FuncType, i_module->funcTypes, i_module->numFuncTypes, funcTypeIndex);
            _throwifnull (i_module->funcTypes);

            // add functype object to the environment & module table
            Environment_AddFuncType (i_module->environment, & ftype); // potentially frees incoming fttype & returns preexisting duplicate
            i_module->funcTypes [funcTypeIndex] = ftype;
            ftype = NULL; // prevent freeing below
        }

        index = (i32) i_module->numFunctions;
_       (Module_AddFunction (i_module, funcTypeIndex, NULL));
        function = Module_GetFunction (i_module, index);

        * io_functionIndex = index;
    }

    function->compiled = NULL;

    if (function->ownsWasmCode)
	{
		m3_Free (function->wasm);
		function->ownsWasmCode = false;
	}

    size_t numBytes = end - i_wasmBytes;
    function->wasm = m3_CopyMem (i_wasmBytes, numBytes);
    _throwifnull (function->wasm);

    function->wasmEnd = function->wasm + numBytes;
    function->ownsWasmCode = true;

    function->module = i_module;

    if (i_doCompilation and not i_module->runtime)
        _throw ("module must be loaded into runtime to compile function");

_   (CompileFunction (function));

	} _catch:
	
    m3_Free (ftype); // freed unless Environment_AddFuncType was called

    return result;
}


M3Result  w3x_AddFunctionToTable  (IM3Function            i_function,
                                  uint32_t *            o_elementIndex,
                                  uint32_t              i_tableIndex)
{
    M3Result result = m3Err_none;

    if (i_function and o_elementIndex)
    {
        IM3Module module = i_function->module;

        if (module)
        {
            u32 previousSize = module->table0Size;
            u32 newTableSize = previousSize + 1;
            module->table0 = m3_ReallocArray (IM3Function, module->table0, newTableSize, previousSize);
            _throwifnull (module->table0);

            * o_elementIndex = previousSize;
            module->table0 [previousSize] = i_function;
            module->table0Size = newTableSize;
        }
        else _throw ("null module");

    }
    else _throw (m3Err_nullArgument);

    _catch: return result;
}


IM3Function  m3_GetFunctionByIndex  (IM3Module i_module, uint32_t i_index)
{
    return Module_GetFunction (i_module, i_index);
}


M3Result  m3_GetFunctionIndex  (IM3Function         i_function,
                                uint32_t *          o_index)
{
    if (i_function and o_index)
    {
        * o_index = i_function->index;

        return m3Err_none;
    }
    else return m3Err_functionLookupFailed;
}


M3Result            m3_GetDataSegmentOffset     (IM3Module              i_module,
                                                 uint32_t               i_index)
{
    M3Result result = m3Err_none;                                       d_m3Assert (i_module);

    if (i_module)
    {
        d_m3Assert (false); // TODO: finish
    }
    else _throw (m3Err_nullArgument);

    _catch: return result;

}


M3Result            m3_RegisterCustomOpcode     (IM3Module              i_module,
                                                 uint16_t               i_opcode,
                                                 uint8_t                i_numImmediates,
                                                 IM3Operation           i_operation)
{
    M3Result result = m3Err_none;                                       d_m3Assert (i_module);

    if (i_module)
    {

    }
    else _throw (m3Err_nullArgument);

    _catch: return result;
}
