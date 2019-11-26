//
//  m3_module.c
//  m3
//
//  Created by Steven Massey on 5/7/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//


#include "m3_module.h"
#include <assert.h>

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
    M3Result result = c_m3Err_none;

    u32 index = io_module->numGlobals++;
    io_module->globals = (M3Global*)m3RellocArray (io_module->globals, M3Global, io_module->numGlobals, index);

    if (io_module->globals)
    {
        M3Global * global = & io_module->globals [index];

        global->type = i_type;
        global->imported = i_isImported;

        if (o_global)
            * o_global = global;
    }
    else result = c_m3Err_mallocFailed;

    return result;
}


M3Result  Module_AddFunction  (IM3Module io_module, u32 i_typeIndex, IM3ImportInfo i_importInfo)
{
    M3Result result = c_m3Err_none;

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
    else result = c_m3Err_mallocFailed;

    return result;
}


IM3Function  Module_GetFunction  (IM3Module i_module, u32 i_functionIndex)
{
    IM3Function func = NULL;

    if (i_functionIndex < i_module->numFunctions)
        func = & i_module->functions [i_functionIndex];

    return func;
}


M3Result  Module_EnsureMemorySize  (IM3Module i_module, M3Memory * io_memory, size_t i_memorySize)
{
    M3Result result = c_m3Err_none;

    // TODO: Handle case when memory is not there at all
    //if (i_memorySize <= io_memory->virtualSize)
    //{
    	const size_t i_memorySizeFull = i_memorySize + sizeof (M3MemoryHeader);
        size_t actualSize = 0;

        if (io_memory->mallocated)
            actualSize = (u8 *) io_memory->mallocated->end - (u8 *) io_memory->mallocated;

        if (i_memorySizeFull > actualSize)
        {
            io_memory->mallocated = (M3MemoryHeader *)m3Realloc (io_memory->mallocated, i_memorySizeFull, actualSize);

            m3log (runtime, "resized WASM linear memory to %lu bytes (%p)", i_memorySize, io_memory->mallocated);

            if (io_memory->mallocated)
            {
                // store pointer to module and end of memory. gives the runtime access to this info.
                io_memory->mallocated->module = i_module;
                io_memory->mallocated->end = (u8 *)(io_memory->mallocated) + i_memorySizeFull;

                io_memory->wasmPages = (u8 *) (io_memory->mallocated + 1);

//              printf ("start= %p  end= %p \n", ptr, end);

                io_memory->heapOffset = i_memorySize;
            }
            else result = c_m3Err_mallocFailed;
        }
    //}
    //else result = c_m3Err_wasmMemoryOverflow;

    return result;
}


i32  AllocateHeap  (M3Memory * io_memory, i32 i_size)
{
    i_size = (i_size + 7) & ~7;
    size_t ptrOffset = io_memory->heapOffset + (io_memory->heapAllocated += i_size);

    size_t size = (u8 *) io_memory->mallocated->end - io_memory->wasmPages;

    d_m3AssertFatal (ptrOffset < size);

    return (i32) ptrOffset;
}


