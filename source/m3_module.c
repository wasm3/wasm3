//
//  m3_module.c
//
//  Created by Steven Massey on 5/7/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_exception.h"


void Module_FreeFunctions (IM3Module i_module)
{
    for (u32 i = 0; i < i_module->numFunctions; ++i)
    {
        IM3Function func = & i_module->functions [i];
        Function_Release (func);
    }
}


void  m3_FreeModule  (IM3Module i_module)
{
    if (i_module)
    {
        m3log (module, "freeing module: %s (funcs: %d; segments: %d)",
               i_module->name, i_module->numFunctions, i_module->numDataSegments);

        Module_FreeFunctions (i_module);

        m3Free (i_module->functions);
        //m3Free (i_module->imports);
        m3Free (i_module->funcTypes);
        m3Free (i_module->dataSegments);
        m3Free (i_module->table0);

        // TODO: free importinfo
        m3Free (i_module->globals);

        m3Free (i_module);
    }
}


M3Result  Module_AddGlobal  (IM3Module io_module, IM3Global * o_global, u8 i_type, bool i_mutable, bool i_isImported)
{
    M3Result result = m3Err_none;
_try {
    u32 index = io_module->numGlobals++;
_   (m3ReallocArray (& io_module->globals, M3Global, io_module->numGlobals, index));

    M3Global * global = & io_module->globals [index];

    global->type = i_type;
    global->imported = i_isImported;
    global->isMutable = i_mutable;

    if (o_global)
        * o_global = global;

} _catch:
    return result;
}


M3Result  Module_AddFunction  (IM3Module io_module, u32 i_typeIndex, IM3ImportInfo i_importInfo)
{
    M3Result result = m3Err_none;
_try {
    u32 index = io_module->numFunctions++;
_   (m3ReallocArray (& io_module->functions, M3Function, io_module->numFunctions, index));

    _throwif("type sig index out of bounds", i_typeIndex >= io_module->numFuncTypes);

    IM3FuncType ft = io_module->funcTypes [i_typeIndex];

    IM3Function func = Module_GetFunction (io_module, index);
    func->funcType = ft;
#if d_m3EnableStrace >= 2
    func->index = index;
#endif

    if (i_importInfo and func->numNames == 0)
    {
        func->import = * i_importInfo;
        func->numNames = 1;
        func->names[0] = i_importInfo->fieldUtf8;
    }

    //          m3log (module, "   added function: %3d; sig: %d", index, i_typeIndex);

} _catch:
    return result;
}


IM3Function  Module_GetFunction  (IM3Module i_module, u32 i_functionIndex)
{
    IM3Function func = NULL;

    if (i_functionIndex < i_module->numFunctions)
        func = & i_module->functions [i_functionIndex];

    return func;
}


const char*  m3_GetModuleName  (IM3Module i_module)
{
    if (!i_module || !i_module->name)
        return "<unknown>";

    return i_module->name;
}

IM3Runtime  m3_GetModuleRuntime  (IM3Module i_module)
{
    return i_module ? i_module->runtime : NULL;
}

