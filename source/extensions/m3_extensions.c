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


IM3Module  m3_NewModule  (IM3Environment i_environment)
{
    IM3Module module = m3_AllocStruct (M3Module);

    if (module)
    {
        module->name = ".unnamed";
        module->startFunction = -1;
        module->environment = i_environment;

        module->wasmStart = NULL;
        module->wasmEnd = NULL;
    }

    return module;
}



M3Result  m3_InjectFunction  (IM3Module                 i_module,
                              int32_t *                 io_functionIndex,
                              const char * const        i_signature,
                              const uint8_t * const     i_wasmBytes,
                              bool                      i_doCompilation)
{
    M3Result result = m3Err_none;                                       d_m3Assert (io_functionIndex);

    IM3Function function = NULL;
    IM3FuncType ftype = NULL;
_   (SignatureToFuncType (& ftype, i_signature));

    i32 index = * io_functionIndex;

    bytes_t bytes = i_wasmBytes;
    bytes_t end = i_wasmBytes + 5;

    u32 size;
_   (ReadLEB_u32 (& size, & bytes, end));
    end = bytes + size;

    if (index >= 0)
    {
        _throwif ("function index out of bounds", index >= i_module->numFunctions);

        function = & i_module->functions [index];

        if (not AreFuncTypesEqual (ftype, function->funcType))
            _throw ("function type mismatch");
    }
    else
    {
        // add slot to function type table in the module
        u32 funcTypeIndex = i_module->numFuncTypes++;
        i_module->funcTypes = m3_ReallocArray (IM3FuncType, i_module->funcTypes, i_module->numFuncTypes, funcTypeIndex);
        _throwifnull (i_module->funcTypes);

        // add functype object to the environment
        Environment_AddFuncType (i_module->environment, & ftype);
        i_module->funcTypes [funcTypeIndex] = ftype;
        ftype = NULL; // prevent freeing below

        index = (i32) i_module->numFunctions;
_       (Module_AddFunction (i_module, funcTypeIndex, NULL));
        function = Module_GetFunction (i_module, index);

        * io_functionIndex = index;
    }

    function->compiled = NULL;

    if (function->ownsWasmCode)
        m3_Free (function->wasm);

    size_t numBytes = end - i_wasmBytes;
    function->wasm = m3_CopyMem (i_wasmBytes, numBytes);
    _throwifnull (function->wasm);

    function->wasmEnd = function->wasm + numBytes;
    function->ownsWasmCode = true;

    function->module = i_module;

    if (i_doCompilation and not i_module->runtime)
        _throw ("module must be loaded into runtime to compile function");

_   (CompileFunction (function));

    _catch:
    m3_Free (ftype);

    return result;
}


IM3Function  m3_GetFunctionByIndex  (IM3Module i_module, uint32_t i_index)
{
    return Module_GetFunction (i_module, i_index);
}
