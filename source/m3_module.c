//
//  m3_module.c
//  m3
//
//  Created by Steven Massey on 5/7/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//


#include "m3_env.h"

void  m3_FreeModule  (IM3Module i_module)
{
    if (i_module)
    {
        m3log (module, "freeing module: %s (funcs: %d; segments: %d)",
               i_module->name, i_module->numFunctions, i_module->numDataSegments);

        m3Free (i_module->functions);
        m3Free (i_module->imports);
        m3Free (i_module->funcTypes);
        m3Free (i_module->dataSegments);

        // TODO: free importinfo
        m3Free (i_module->globals);

        m3Free (i_module);
    }
}


M3Result  Module_AddGlobal  (IM3Module io_module, IM3Global * o_global, u8 i_type, bool i_mutable, bool i_isImported)
{
    M3Result result = m3Err_none;

    u32 index = io_module->numGlobals++;
    io_module->globals = (M3Global *) m3RellocArray (io_module->globals, M3Global, io_module->numGlobals, index);

    if (io_module->globals)
    {
        M3Global * global = & io_module->globals [index];

        global->type = i_type;
        global->imported = i_isImported;
        global->isMutable = i_mutable;

        if (o_global)
            * o_global = global;
    }
    else result = m3Err_mallocFailed;

    return result;
}


M3Result  Module_AddFunction  (IM3Module io_module, u32 i_typeIndex, IM3ImportInfo i_importInfo)
{
    M3Result result = m3Err_none;

    u32 index = io_module->numFunctions++;
    io_module->functions = (M3Function*)m3RellocArray (io_module->functions, M3Function, io_module->numFunctions, index);

    if (io_module->functions)
    {
        if (i_typeIndex < io_module->numFuncTypes)
        {
            IM3FuncType ft = & io_module->funcTypes [i_typeIndex];

            IM3Function func = Module_GetFunction (io_module, index);
            func->funcType = ft;

            if (i_importInfo)
            {
                func->import = * i_importInfo;
                func->name = i_importInfo->fieldUtf8;
            }

            //          m3log (module, "   added function: %3d; sig: %d", index, i_typeIndex);
        }
        else result = "unknown type sig index";
    }
    else result = m3Err_mallocFailed;

    return result;
}


IM3Function  Module_GetFunction  (IM3Module i_module, u32 i_functionIndex)
{
    IM3Function func = NULL;

    if (i_functionIndex < i_module->numFunctions)
        func = & i_module->functions [i_functionIndex];

    return func;
}

