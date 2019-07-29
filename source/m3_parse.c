//
//  m3_parse.c
//  M3: Massey Meta Machine
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_compile.h"
#include "m3_exec.h"
#include "m3_exception.h"


M3Result  ParseType_Table  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
	M3Result result = c_m3Err_none;

	return result;
}


M3Result  ParseSection_Type  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
	M3Result result = c_m3Err_none;
	
	u32 numTypes;
_	(ReadLEB_u32 (& numTypes, & i_bytes, i_end));									m3log (parse, "** Type [%d]", numTypes);
	
	if (numTypes)
	{
		// FIX: these need to be instead added to a set in the runtime struct to facilitate IndirectCall
		
_		(m3Alloc (& io_module->funcTypes, M3FuncType, numTypes));
		
		io_module->numFuncTypes = numTypes;
		
		IM3FuncType ft = io_module->funcTypes;
		
		while (numTypes--)
		{
			i8 form;
_			(ReadLEB_i7 (& form, & i_bytes, i_end));
			
			if (form != -32)
				throw (c_m3Err_wasmMalformed); // for WA MVP				}
			
_			(ReadLEB_u32 (& ft->numArgs, & i_bytes, i_end));
			
			if (ft->numArgs <= c_m3MaxNumFunctionArgs)
			{
				for (u32 i = 0; i < ft->numArgs; ++i)
				{
					i8 argType;
_					(ReadLEB_i7 (& argType, & i_bytes, i_end));
					
					ft->argTypes [i] = -argType;
				}
			}
			else throw (c_m3Err_typeListOverflow);
			
			u8 returnCount;
_			(ReadLEB_u7 /* u1 in spec */ (& returnCount, & i_bytes, i_end));

			if (returnCount)
			{
				i8 returnType;
_				(ReadLEB_i7 (& returnType, & i_bytes, i_end));
_				(NormalizeType (& ft->returnType, returnType));
			}																		m3logif (parse, PrintFuncTypeSignature (ft))

			++ft;
		}
	}

	catch:
	
	if (result)
	{
		free (io_module->funcTypes);
		io_module->funcTypes = NULL;
		io_module->numFuncTypes = 0;
	}

	return result;
}


M3Result  ParseSection_Function  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
	M3Result result = c_m3Err_none;
	
	u32 numFunctions;
_	(ReadLEB_u32 (& numFunctions, & i_bytes, i_end));								m3log (parse, "** Function [%d]", numFunctions);

	for (u32 i = 0; i < numFunctions; ++i)
	{
		u32 funcTypeIndex;
_		(ReadLEB_u32 (& funcTypeIndex, & i_bytes, i_end));

_		(Module_AddFunction (io_module, funcTypeIndex, NULL /* import info */));
	}
	
	catch: return result;
}


M3Result  ParseSection_Import  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
	M3Result result = c_m3Err_none;
	
	M3ImportInfo import = {}, clearImport = {};
	
	u32 numImports;
_	(ReadLEB_u32 (& numImports, & i_bytes, i_end));									m3log (parse, "** Import [%d]", numImports);
	
	for (u32 i = 0; i < numImports; ++i)
	{
		u8 importKind;
		
_		(Read_utf8 (& import.moduleUtf8, & i_bytes, i_end));
_		(Read_utf8 (& import.fieldUtf8, & i_bytes, i_end));
_		(Read_u8 (& importKind, & i_bytes, i_end));									m3log (parse, "  - kind: %d; '%s.%s' ",
																					   			(u32) importKind, import.moduleUtf8, import.fieldUtf8);
		switch (importKind)
		{
			case c_externalKind_function:
			{
				u32 typeIndex;
_				(ReadLEB_u32 (& typeIndex, & i_bytes, i_end))
				
_				(Module_AddFunction (io_module, typeIndex, & import))
				import = clearImport;
				
				io_module->numImports++;
			}
			break;
			
			case c_externalKind_table:
//					abort ();
//					result = ParseType_Table (& i_bytes, i_end);
				break;
				
			case c_externalKind_memory:
			{
				u8 flag;
				u32 pages, maxPages = 0;

_				(ReadLEB_u7 (& flag, & i_bytes, i_end));	// really a u1
_				(ReadLEB_u32 (& pages, & i_bytes, i_end));
				
				if (flag)
_ 					(ReadLEB_u32 (& maxPages, & i_bytes, i_end));

				io_module->memory.virtualSize = pages * c_m3MemPageSize;			m3log (parse, "     memory: pages: %d max: %d", pages, maxPages);
			}
			break;
				
			case c_externalKind_global:
			{
				i8 waType;
				u8 type, mutable;
				
_				(ReadLEB_i7 (& waType, & i_bytes, i_end));
_				(NormalizeType (& type, waType));
_				(ReadLEB_u7 (& mutable, & i_bytes, i_end));							m3log (parse, "     global: %s mutable=%d", c_waTypes [type], (u32) mutable);
				
				IM3Global global;
_				(Module_AddGlobal (io_module, & global, type, mutable, true /* isImport */));
				global->import = import;
				import = clearImport;
			}
			break;
			
			default:
				throw (c_m3Err_wasmMalformed);
		}

		FreeImportInfo (& import);
	}

	catch:
	{
		FreeImportInfo (& import);
	}
	
	return result;
}


M3Result  ParseSection_Export  (IM3Module io_module, bytes_t i_bytes, cbytes_t	i_end)
{
	M3Result result = c_m3Err_none;
	
	u32 numExports;
_	(ReadLEB_u32 (& numExports, & i_bytes, i_end));									m3log (parse, "** Export [%d]", numExports);
	
	for (u32 i = 0; i < numExports; ++i)
	{
		const char * utf8;
		u8 exportKind;
		u32 index;

_		(Read_utf8 (& utf8, & i_bytes, i_end));
_		(Read_u8 (& exportKind, & i_bytes, i_end));
_		(ReadLEB_u32 (& index, & i_bytes, i_end));									m3log (parse, "  - index: %4d; kind: %d; export: '%s'; ", index, (u32) exportKind, utf8);
		
		if (exportKind == c_externalKind_function)
		{
			if (not io_module->functions [index].name)
			{
				io_module->functions [index].name = utf8;
				utf8 = NULL; // ownership transfered to M3Function
			}
		}
		
		free ((void *) utf8);
	}
	
	catch: return result;
}



M3Result  Parse_InitExpr  (M3Module * io_module, bytes_t * io_bytes, cbytes_t i_end)
{
	M3Result result = c_m3Err_none;
	
	// this doesn't generate code pages. just walks the wasm bytecode to find the end
	IM3Runtime rt;
	M3Compilation compilation = { rt= NULL, io_module, * io_bytes, i_end };
	
	result = Compile_BlockStatements (& compilation);
	
	* io_bytes = compilation.wasm;
	
	return result;
}



M3Result  ParseSection_Element  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
	M3Result result = c_m3Err_none;

	u32 numSegments;
	result = ReadLEB_u32 (& numSegments, & i_bytes, i_end);							m3log (parse, "** Element [%d]", numSegments);

	if (not result)
	{
		io_module->elementSection = i_bytes;
		io_module->elementSectionEnd = i_end;
		io_module->numElementSegments = numSegments;
	}
	else result = "error parsing Element section";
	
	return result;
}



M3Result  ParseSection_Code  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
	M3Result result;

	u32 numFunctions;
_	(ReadLEB_u32 (& numFunctions, & i_bytes, i_end));								m3log (parse, "** Code [%d]", numFunctions);
	
	if (numFunctions != io_module->numFunctions - io_module->numImports)
	{
		numFunctions = 0;
		throw (c_m3Err_wasmMalformed);	// FIX: better error
	}
	
	for (u32 f = 0; f < numFunctions; ++f)
	{
		u32 size;
_		(ReadLEB_u32 (& size, & i_bytes, i_end));
		
		if (size)
		{
			const u8 * ptr = i_bytes;
			i_bytes += size;
			
			if (i_bytes <= i_end)
			{
				const u8 * start = ptr;
				
				u32 numLocals;
_				(ReadLEB_u32 (& numLocals, & ptr, i_end));							m3log (parse, "  - func size: %d; locals: %d", size, numLocals);
				
				u32 numLocalVars = 0;
				
				for (u32 l = 0; l < numLocals; ++l)
				{
					u32 varCount;
					i8 varType;
					u8 normalizedType;

_					(ReadLEB_u32 (& varCount, & ptr, i_end));
_					(ReadLEB_i7 (& varType, & ptr, i_end));
_					(NormalizeType (& normalizedType, varType));

					numLocalVars += varCount;										m3log (parse, "    - %d locals; type: '%s'", varCount, c_waTypes [-varType]);
				}
				
				IM3Function func = Module_GetFunction (io_module, f + io_module->numImports);
				
				func->module = io_module;
				func->wasm = start;
				func->wasmEnd = i_bytes;
				func->numLocals = numLocalVars;
			}
			else throw (c_m3Err_wasmSectionOverrun);
		}
	}

	catch:
	
	if (not result and i_bytes != i_end)
		result = c_m3Err_wasmSectionUnderrun;
	
	return result;
}


M3Result  ParseSection_Data  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
	M3Result result = c_m3Err_none;
	
	u32 numDataSegments;
_	(ReadLEB_u32 (& numDataSegments, & i_bytes, i_end));							m3log (parse, "** Data [%d]", numDataSegments);
	
_	(m3Alloc (& io_module->dataSegments, M3DataSegment, numDataSegments));

	io_module->numDataSegments = numDataSegments;

	for (u32 i = 0; i < numDataSegments; ++i)
	{
		M3DataSegment * segment = & io_module->dataSegments [i];
		
_	 	(ReadLEB_u32 (& segment->memoryRegion, & i_bytes, i_end));

		segment->initExpr = i_bytes;
_ 		(Parse_InitExpr (io_module, & i_bytes, i_end));
		segment->initExprSize = (u32) (i_bytes - segment->initExpr);
		
		if (segment->initExprSize <= 1)
			throw (c_m3Err_wasmMissingInitExpr);
		
_ 		(ReadLEB_u32 (& segment->size, & i_bytes, i_end));

		segment->data = i_bytes;													m3log (parse, "    segment [%u]  memory: %u;  expr-size: %d;  size: %d",
																					   i, segment->memoryRegion, segment->initExprSize, segment->size);
	}

	catch:
	// TODO failure cleanup

	return result;
}


M3Result  ParseSection_Global  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
	M3Result result = c_m3Err_none;
	
	u32 numGlobals;
_	(ReadLEB_u32 (& numGlobals, & i_bytes, i_end));									m3log (parse, "** Global [%d]", numGlobals);
	
	for (u32 i = 0; i < numGlobals; ++i)
	{
		i8 waType;
		u8 type;
		
_ 		(ReadLEB_i7 (& waType, & i_bytes, i_end));
_ 		(NormalizeType (& type, waType));											m3log (parse, "  - add global: [%d] %s", i, c_waTypes [type]);
		
		IM3Global global;
_ 		(Module_AddGlobal (io_module, & global, type, false /* mutable */, false /* isImport */));
		
		global->initExpr = i_bytes;
_		(Parse_InitExpr (io_module, & i_bytes, i_end));
		global->initExprSize = (u32) (i_bytes - global->initExpr);
		
		if (global->initExprSize <= 1)
			throw (c_m3Err_wasmMissingInitExpr);
	}

	catch: return result;
}


M3Result  ParseSection_Custom  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
	M3Result result;

	cstr_t name;
_	(Read_utf8 (& name, & i_bytes, i_end));
																					m3log (parse, "** Custom: '%s'", name);
	if (strcmp (name, "name") != 0)
		i_bytes = i_end;
	
	free ((void *) name);

	while (i_bytes < i_end)
	{
		u8 nameType;
		u32 payloadLength;
		
_		(ReadLEB_u7 (& nameType, & i_bytes, i_end));
_		(ReadLEB_u32 (& payloadLength, & i_bytes, i_end));
		
		if (nameType == 1)
		{
			u32 numNames;
_			(ReadLEB_u32 (& numNames, & i_bytes, i_end));
			
			for (u32 i = 0; i < numNames; ++i)
			{
				u32 index;
_				(ReadLEB_u32 (& index, & i_bytes, i_end));
_				(Read_utf8 (& name, & i_bytes, i_end));
				
				if (index < io_module->numFunctions)
				{
					if (not io_module->functions [index].name)
					{
						io_module->functions [index].name = name;					m3log (parse, "naming function [%d]: %s", index, name);
						name = NULL;
					}
//							else m3log (parse, "prenamed: %s", io_module->functions [index].name);
				}
				
				free ((void *) name);
			}
		}
		
		i_bytes += payloadLength;
	}
	
	catch: return result;
}


M3Result  ParseModuleSection  (M3Module * o_module, u8 i_sectionType, bytes_t i_bytes, u32 i_numBytes)
{
	M3Result result = c_m3Err_none;
	
	typedef M3Result (* M3Parser) (M3Module *, bytes_t, cbytes_t);
	
	static M3Parser s_parsers [] =
	{
		ParseSection_Custom,	// 0
		ParseSection_Type,		// 1
		ParseSection_Import,	// 2
		ParseSection_Function,	// 3
		NULL,					// 4: table
		NULL,					// 5: memory
		ParseSection_Global,	// 6
		ParseSection_Export,	// 7
		NULL,					// 8: start
		ParseSection_Element,	// 9
		ParseSection_Code,		// 10
		ParseSection_Data		// 11
	};
	
	M3Parser parser = NULL;
	
	if (i_sectionType <= 11)
		parser = s_parsers [i_sectionType];
	
	if (parser)
	{
		cbytes_t end = i_bytes + i_numBytes;
		result = parser (o_module, i_bytes, end);
	}
	else m3log (parse, "<skipped (id: %d)>", (u32) i_sectionType);
	
	return result;
}


M3Result  m3_ParseModule  (IM3Module * o_module, cbytes_t i_bytes, u32 i_numBytes)
{
	M3Result result;

	IM3Module module;
_	(m3Alloc (& module, M3Module, 1));
//	Module_Init (module);

	module->name = ".unnamed";														m3log (parse, "load module: %d bytes", i_numBytes);
	
	const u8 * pos = i_bytes;
	const u8 * end = pos + i_numBytes;
	
	u32 magic = 0;
_	(Read_u32 (& magic, & pos, end));
	
	if (magic == 0x6d736100)
	{
		u32 version;
_		(Read_u32 (&version, & pos, end));
		
		if (version == 1)
		{																			m3log (parse,  "found magic + version");
			u8 previousSection = 0;
			
			while (pos < end)
			{
				u8 sectionCode;
_				(ReadLEB_u7 (& sectionCode, & pos, end));
				
				if (sectionCode > previousSection or sectionCode == 0)				// from the spec: sections must appear in order
				{
					u32 sectionLength;
_					(ReadLEB_u32 (& sectionLength, & pos, end));
_					(ParseModuleSection (module, sectionCode, pos, sectionLength));
					
					pos += sectionLength;
					
					if (sectionCode)
						previousSection = sectionCode;
				}
				else throw (c_m3Err_misorderedWasmSection);
			}
		}
		else throw (c_m3Err_incompatibleWasmVersion);
	}
	else throw (c_m3Err_wasmMalformed);

	catch:
	
	if (result)
	{
		m3_FreeModule (module);
		module = NULL;
	}
	
	* o_module = module;
	
	return result;
}

