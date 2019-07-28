//
//  m3_env.c
//  M3: Massey Meta Machine
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include <assert.h>
#include <stdarg.h>

#include "m3.h"
#include "m3_module.h"
#include "m3_compile.h"
#include "m3_exec.h"
#include "m3_exception.h"

ccstr_t  GetFunctionName  (IM3Function i_function)
{
	return (i_function->name) ? i_function->name : ".unnamed";
}


u32  GetFunctionNumArgs  (IM3Function i_function)
{
	u32 numArgs = 0;
	
	if (i_function->funcType)
		numArgs = i_function->funcType->numArgs;
	
	return numArgs;
}


u32  GetFunctionNumReturns  (IM3Function i_function)
{
	u32 numReturns = 0;
	
	if (i_function->funcType)
		numReturns = i_function->funcType->returnType ? 1 : 0;
	
	return numReturns;
}


u8  GetFunctionReturnType  (IM3Function i_function)
{
	u8 returnType = c_m3Type_none;
	
	if (i_function->funcType)
		returnType = i_function->funcType->returnType;
	
	return returnType;
}


u32  GetFunctionNumArgsAndLocals (IM3Function i_function)
{
	if (i_function)
		return i_function->numLocals + GetFunctionNumArgs (i_function);
	else
		return 0;
}


void FreeImportInfo (M3ImportInfo * i_info)
{
	free ((void *) i_info->moduleUtf8);	i_info->moduleUtf8 = NULL;
	free ((void *) i_info->fieldUtf8);	i_info->fieldUtf8 = NULL;
}


void  InitRuntime  (IM3Runtime io_runtime, u32 i_stackSizeInBytes)
{
	m3Malloc (& io_runtime->stack, i_stackSizeInBytes);
	io_runtime->numStackSlots = i_stackSizeInBytes / sizeof (m3word_t);
}


IM3Runtime  m3_NewRuntime  (u32 i_stackSizeInBytes)
{
	m3_PrintM3Info ();
	
	IM3Runtime env;
	m3Alloc (& env, M3Runtime, 1);
	
	if (env)
		InitRuntime (env, i_stackSizeInBytes);
	
	return env;
}


typedef void * (* ModuleVisitor) (IM3Module i_module, void * i_info);

void *  ForEachModule  (IM3Runtime i_runtime, ModuleVisitor i_visitor, void * i_info)
{
	void * r = NULL;
	
	IM3Module module = i_runtime->modules;
	
	while (module)
	{
		IM3Module next = module->next;
		r = i_visitor (module, i_info);
		if (r)
			break;
		
		module = next;
	}
	
	return r;
}


void *  _FreeModule  (IM3Module i_module, void * i_info)
{
	m3_FreeModule (i_module);
	return NULL;
}


void  ReleaseRuntime  (IM3Runtime i_runtime)
{
	ForEachModule (i_runtime, _FreeModule, NULL);
	
	FreeCodePages (i_runtime->pagesOpen);
	FreeCodePages (i_runtime->pagesFull);
	
	free (i_runtime->stack);
}


void  m3_FreeRuntime  (IM3Runtime i_runtime)
{
	if (i_runtime)
	{
		ReleaseRuntime (i_runtime);
		free (i_runtime);
	}
}


M3Result  EvaluateExpression  (IM3Module i_module, void * o_expressed, u8 i_type, bytes_t * io_bytes, cbytes_t i_end)
{
	M3Result result = c_m3Err_none;
//	* o_expressed = 0;
	
	u64 stack [c_m3MaxFunctionStackHeight];	// stack on the stack

	// create a temporary runtime context
	M3Runtime rt = {};
	rt.numStackSlots = c_m3MaxFunctionStackHeight;
	rt.stack = & stack;
	
	IM3Runtime savedRuntime = i_module->runtime;
	i_module->runtime = & rt;
	
	M3Compilation o = { & rt, i_module, * io_bytes, i_end };
	o.block.depth = -1;  // so that root complation depth = 0
	
	IM3CodePage page = o.page = AcquireCodePage (& rt);
	
	if (page)
	{
		pc_t m3code = GetPagePC (page);
		result = CompileBlock (& o, i_type, 0);
		
		if (not result)
		{
			m3ret_t r = Call (m3code, stack, NULL, d_m3OpDefaultArgs);
			result = rt.runtimeError;
			
			if (r == 0 and not result)
			{
				if (SizeOfType (i_type) == sizeof (u32))
				{
					* (u32 *) o_expressed = * (u32 *) stack;
				}
				else if (SizeOfType (i_type) == sizeof (u64))
				{
					* (u64 *) o_expressed = * (u64 *) stack;
				}
			}
		}
		
		ReleaseCodePage (& rt, page);
	}
	else result = c_m3Err_mallocFailedCodePage;
	
	rt.stack = NULL; 				// prevent free(stack) in ReleaseRuntime ()
	ReleaseRuntime (& rt);
	i_module->runtime = savedRuntime;
	
	* io_bytes = o.wasm;
	
	return result;
}


M3Result  EvaluateExpression_i32  (IM3Module i_module, i32 * o_expressed, bytes_t * i_bytes, cbytes_t i_end)
{
	return EvaluateExpression (i_module, o_expressed, c_m3Type_i32, i_bytes, i_end);
}


M3Result  InitGlobals  (IM3Module io_module)
{
	M3Result result = c_m3Err_none;
	
	if (io_module->numGlobals)
	{
		// placing the globals in their structs isn't good for cache locality, but i don't really know what the global
		// access patterns typcially look like yet.
		
		//			io_module->globalMemory = m3Alloc (m3reg_t, io_module->numGlobals);
		
		//			if (io_module->globalMemory)
		{
			for (u32 i = 0; i < io_module->numGlobals; ++i)
			{
				M3Global * g = & io_module->globals [i];						m3log (runtime, "initializing global: %d", i);
				
				// global fp types are coerced to double
				if (g->type == c_m3Type_f32)
					g->type = c_m3Type_f64;
				
				if (g->initExpr)
				{
					bytes_t start = g->initExpr;
					result = EvaluateExpression (io_module, & g->intValue, g->type, & start, g->initExpr + g->initExprSize);
					
					if (not result)
					{
						//						io_module->globalMemory [i] = initValue;
					}
					else break;
				}
				else
				{																m3log (runtime, "importing global");
					
				}
			}
		}
		//			else result = ErrorModule (c_m3Err_mallocFailed, io_module, "could allocate globals for module: '%s", io_module->name);
	}

	return result;
}


M3Result  InitDataSegments  (IM3Module io_module)
{
	M3Result result = c_m3Err_none;

	for (u32 i = 0; i < io_module->numDataSegments; ++i)
	{
		M3DataSegment * segment = & io_module->dataSegments [i];
		
		i32 segmentOffset;
		bytes_t start = segment->initExpr;
_		(EvaluateExpression_i32 (io_module, & segmentOffset, & start, segment->initExpr + segment->initExprSize));
		
		u32 minMemorySize = segment->size + segmentOffset + 1;						m3log (runtime, "loading data segment: %d  offset: %d", i, segmentOffset);
_		(Module_EnsureMemorySize (io_module, & io_module->memory, minMemorySize));
		
		memcpy (io_module->memory.wasmPages + segmentOffset, segment->data, segment->size);
	}

	catch: return result;
}


M3Result  InitElements  (IM3Module io_module)
{
	M3Result result = c_m3Err_none;
	
	bytes_t bytes = io_module->elementSection;
	cbytes_t end = io_module->elementSectionEnd;
	
	for (u32 i = 0; i < io_module->numElementSegments; ++i)
	{
		u32 index;
_		(ReadLEB_u32 (& index, & bytes, end));
		
		if (index == 0)
		{
			i32 offset;
_			(EvaluateExpression_i32 (io_module, & offset, & bytes, end));
			
			u32 numElements;
_			(ReadLEB_u32 (& numElements, & bytes, end));

			u32 endElement = numElements + offset;
			
			if (endElement > offset)
			{
				io_module->table0 = m3RellocArray (io_module->table0, IM3Function, endElement, io_module->table0Size);
				
				if (io_module->table0)
				{
					io_module->table0Size = endElement;
					
					for (u32 e = 0; e < numElements; ++e)
					{
						u32 functionIndex;
_						(ReadLEB_u32 (& functionIndex, & bytes, end));
						
						if (functionIndex < io_module->numFunctions)
						{
							IM3Function function = & io_module->functions [functionIndex];		d_m3Assert (function); //printf ("table: %s\n", function->name);
							io_module->table0 [e + offset] = function;
						}
						else throw ("function index out of range");
					}
				}
				else throw (c_m3Err_mallocFailed);
			}
			else throw ("table overflow");
		}
		else throw ("element table index must be zero for MVP");
	}
	
	catch: return result;
}


M3Result  m3_LoadModule  (IM3Runtime io_runtime, IM3Module io_module)
{
	M3Result result = c_m3Err_none;
	
	if (not io_module->runtime)
	{
//		assert (io_module->memory.actualSize == 0);
		
_		(InitGlobals (io_module));
_		(InitDataSegments (io_module));
_		(InitElements (io_module));
		
		io_module->runtime = io_runtime;
		io_module->next = io_runtime->modules;
		io_runtime->modules = io_module;
	}
	else throw (c_m3Err_moduleAlreadyLinked);

	catch: return result;
}


void *  v_FindFunction  (IM3Module i_module, const char * const i_name)
{
	for (u32 i = 0; i < i_module->numFunctions; ++i)
	{
		IM3Function f = & i_module->functions [i];
		
		if (f->name)
		{
			if (strcmp (f->name, i_name) == 0)
				return f;
		}
	}
	
	return NULL;
}


M3Result  m3_FindFunction  (IM3Function * o_function, IM3Runtime i_runtime, const char * const i_functionName)
{
	M3Result result = c_m3Err_none;
	
	IM3Function function = ForEachModule (i_runtime, (ModuleVisitor) v_FindFunction, (void *) i_functionName);
	
	if (function)
	{
		if (not function->compiled)
		{
			result = Compile_Function (function);
			if (result)
				function = NULL;
		}
	}
	else result = c_m3Err_functionLookupFailed;
	
	* o_function = function;
	
	return result;
}


M3Result  m3_Call  (IM3Function i_function)
{
	return m3_CallWithArgs (i_function, 0, NULL);
}


M3Result  m3_CallWithArgs  (IM3Function i_function, i32 i_argc, ccstr_t * i_argv)
{
	M3Result result = c_m3Err_none;
	
	if (i_function->compiled)
	{
		IM3Module module = i_function->module;
		
		IM3Runtime env = module->runtime;
		
_		(Module_EnsureMemorySize (module, & i_function->module->memory, 3000000));
		
		u8 * linearMemory = module->memory.wasmPages;

		m3stack_t stack = env->stack;

		if (i_argc)
		{
			IM3Memory memory = & module->memory;
			// FIX: memory allocation in general
			i32 offset = AllocateHeap (memory, sizeof (i32) * i_argc);
			
			i32 * pointers = (i32 *) (memory->wasmPages + offset);
			
			for (i32 i = 0; i < i_argc; ++i)
			{
				size_t argLength = strlen (i_argv [i]) + 1;
				
				if (argLength < 4000)
				{
					i32 o = AllocateHeap (memory, (i32) argLength);
					memcpy (memory->wasmPages + o, i_argv [i], argLength);
					
					* pointers++ = o;
				}
				else throw ("insane argument string length");
			}
			
			stack [0] = i_argc;
			stack [1] = offset;
		}

_		(Call (i_function->compiled, stack, linearMemory, d_m3OpDefaultArgs));

		u64 value = * (u64 *) (stack);
		m3log (runtime, "return64: %llu return32: %u", value, (u32) value);
	}
	else throw (c_m3Err_missingCompiledCode);
	
	catch: return result;
}


IM3CodePage  AcquireCodePage  (IM3Runtime i_runtime)
{
	if (i_runtime->pagesOpen)
		return PopCodePage (& i_runtime->pagesOpen);
	else
		return NewCodePage (500);	// for 4kB page
}


IM3CodePage  AcquireCodePageWithCapacity  (IM3Runtime i_runtime, u32 i_lineCount)
{
	IM3CodePage page;
	
	if (i_runtime->pagesOpen)
	{
		page = PopCodePage (& i_runtime->pagesOpen);
	
		if (NumFreeLines (page) < i_lineCount)
		{
			IM3CodePage tryAnotherPage = AcquireCodePageWithCapacity (i_runtime, i_lineCount);
			
			ReleaseCodePage (i_runtime, page);
			page = tryAnotherPage;
		}
	}
	else page = NewCodePage (i_lineCount);
	
	return page;
}


void  ReleaseCodePage  (IM3Runtime i_runtime, IM3CodePage i_codePage)
{
//	DumpCodePage (i_codePage, /* startPC: */ NULL);
	
	if (i_codePage)
	{
		IM3CodePage * list;
		
		if (NumFreeLines (i_codePage) < c_m3CodePageFreeLinesThreshold)
			list = & i_runtime->pagesFull;
		else
			list = & i_runtime->pagesOpen;
		
		PushCodePage (list, i_codePage);
	}
}


//void  CloseCodePage  (IM3Runtime i_runtime, IM3CodePage i_codePage)
//{
//	i_codePage->info.lineIndex = c_m3CodePageFreeLinesThreshold;
//	ReleaseCodePage (i_runtime, i_codePage);
//}


M3Result  m3Error  (M3Result i_result, IM3Runtime i_runtime, IM3Module i_module, IM3Function i_function,
					const char * const i_file, u32 i_lineNum, const char * const i_errorMessage, ...)
{
	if (i_runtime)
	{
		M3ErrorInfo info = { i_result, i_runtime, i_module, i_function, i_file, i_lineNum };
		
		va_list args;
		va_start (args, i_errorMessage);
		vsnprintf (info.message, 1023, i_errorMessage, args);
		va_end (args);

		i_runtime->error = info;
	}
	
	return i_result;
}


M3ErrorInfo  m3_GetErrorInfo  (IM3Runtime i_runtime)
{
	M3ErrorInfo info = i_runtime->error;
	
	M3ErrorInfo reset = {};
	i_runtime->error = reset;
	
	return info;
}


