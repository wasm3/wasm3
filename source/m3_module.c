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
		
		free (i_module->functions);
		free (i_module->imports);
		free (i_module->funcTypes);
		free (i_module->dataSegments);
		
		// TODO: free importinfo
		free (i_module->globals);
		
		free (i_module);
	}
}


M3Result  Module_AddGlobal  (IM3Module io_module, IM3Global * o_global, u8 i_type, bool i_mutable, bool i_isImported)
{
	M3Result result = c_m3Err_none;
	
	u32 index = io_module->numGlobals++;
	io_module->globals = m3RellocArray (io_module->globals, M3Global, io_module->numGlobals, index);
	
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
	io_module->functions = m3RellocArray (io_module->functions, M3Function, io_module->numFunctions, index);
	
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
			
			//			m3log (module, "   added function: %3d; sig: %d", index, i_typeIndex);
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
	
	if (i_memorySize <= io_memory->virtualSize)
	{
		size_t actualSize = 0;
		
		if (io_memory->mallocated)
			actualSize = (u8 *) io_memory->mallocated->end - (u8 *) io_memory->mallocated;
		
		if (i_memorySize > actualSize)
		{
			i_memorySize = io_memory->virtualSize; // hack
			
			//			m3word_t alignedSize = i_memorySize + sizeof (void *) * 2; // end pointer + module ptr

			// HACK: this is all hacked 'cause I don't understand the Wasm memory. Or it doesn't understand me.
			// Just get'n some tests/benchmarks going for now:
			i32 pages = 2;
			
			size_t extra = c_m3MemPageSize * pages + 900000 * 4 + sizeof (M3MemoryHeader);
			
			size_t alignedSize = i_memorySize + extra;
			
			if (c_m3AlignWasmMemoryToPages)
			{
				size_t aligner = c_m3MemPageSize - 1;
				alignedSize += aligner;
				alignedSize &= ~aligner;
			}
			
			io_memory->mallocated = m3Realloc (io_memory->mallocated, alignedSize, actualSize);
			
			m3log (runtime, "resized WASM linear memory to %llu bytes (%p)", alignedSize, io_memory->mallocated);
			
			if (io_memory->mallocated)
			{
				void * end = (u8 *) io_memory->mallocated + alignedSize;
				u8 * ptr = (u8 *) (io_memory->mallocated + 1);
				
				io_memory->wasmPages = ptr;
				
				// store pointer to module and end of memory. gives the runtime access to this info.
				io_memory->mallocated->module = i_module;
				io_memory->mallocated->end = end;
				
//				printf ("start= %p  end= %p \n", ptr, end);

				io_memory->heapOffset = i_memorySize;
			}
			else result = c_m3Err_mallocFailed;
		}
	}
	else result = c_m3Err_wasmMemoryOverflow;
	
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


