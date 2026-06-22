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

uint32_t  w3x_GetNumFunctions  (IM3Module				i_module)
{
	return (i_module) ? i_module->numFunctions : 0;
}


M3Result            w3x_AllocateFunction        (IM3Module              i_module,
												 int32_t *              io_functionIndex,
												 const char * const     i_name,
												 const char * const     i_signature)
{																						d_m3Assert (io_functionIndex);
	IM3FuncType ftype = NULL;

	_try {
	
	if (not i_module)
		_throw (m3Err_nullArgument);

	IM3Function function = NULL;
_   (SignatureToFuncType (& ftype, i_signature));

	i32 index = * io_functionIndex;

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
		
	Function_Release (function);
	
	if (i_name)
	{
		cstr_t name = m3_Malloc ("function name", strlen (i_name) + 1);			_throwif (m3Err_mallocFailed, not name);
		strcpy ((char *) name, i_name);

		function->names [0] = name;
		function->export_name = name;
		function->numNames = 1;
	}

	} _catch:
	
	m3_Free (ftype); // freed unless Environment_AddFuncType was called

	return result;
}


M3Result			w3x_AttachFunctionCode		(IM3Module				i_module,
												 int32_t				i_functionIndex,
												 const uint8_t * const  i_wasmBytes,
												 const uint32_t			i_numWasmBytes,
												 bool                   i_doCompilation)
{
	_try {																				_throwif (m3Err_nullArgument, not i_module);
		IM3Function function = Module_GetFunction (i_module, (u32) i_functionIndex);	_throwif (m3Err_functionLookupFailed, not function);
		
		bytes_t bytes = i_wasmBytes;
		bytes_t end = i_wasmBytes + i_numWasmBytes;

		u32 size;
_   	(ReadLEB_u32 (& size, & bytes, end));
		bytes_t calculatedEnd = bytes + size;											_throwif (m3Err_wasmOverrun, calculatedEnd > end);
																						_throwif (m3Err_wasmUnderrun, calculatedEnd < end);
		end = calculatedEnd;

		size_t numBytes = end - i_wasmBytes;
		function->wasm = m3_CopyMem (i_wasmBytes, numBytes);
		_throwifnull (function->wasm);

		function->wasmEnd = function->wasm + numBytes;
		function->ownsWasmCode = true;

		function->module = i_module;
		
		if (i_doCompilation)
		{
			_throwif ("module must be loaded into runtime to compile function", not i_module->runtime);
			
_		   	(CompileFunction (function));
		}

	} _catch:

	return result;
}



M3Result  w3x_InjectFunction  (IM3Module                 i_module,
							   int32_t *                 io_functionIndex,
							   const char * const     	 i_name,
							   const char * const        i_signature,
							   const uint8_t * const     i_wasmBytes,
							   const uint32_t			 i_numWasmBytes,
							   bool                      i_doCompilation)
{																						d_m3Assert (io_functionIndex);
	_try {
_		(w3x_AllocateFunction (i_module, io_functionIndex, i_name, i_signature));
_		(w3x_AttachFunctionCode (i_module, * io_functionIndex, i_wasmBytes, i_numWasmBytes, i_doCompilation));

	} _catch:
	
	return result;

/*
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

    if (i_doCompilation)
	{
		_throwif (not i_module->runtime, "module must be loaded into runtime to compile function");
		
_   	(CompileFunction (function));
	}

	} _catch:
	
    m3_Free (ftype); // freed unless Environment_AddFuncType was called

    return result;
 */
}


M3Result  w3x_AddFunctionToTable  (IM3Function          i_function,
                                  uint32_t *            o_elementIndex,
                                  uint32_t              i_tableIndex)
{
    M3Result result = m3Err_none;								_throwif (m3Err_indexOutOfBounds, i_tableIndex != 0);

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


M3Result            w3x_GetDataSegmentInfo      (IM3Module              i_module,
												 uint32_t *				o_offset,
												 uint32_t *				o_size,
												 uint32_t               i_dataSegmentIndex)
{
    M3Result result = m3Err_none;                                       d_m3Assert (i_module);

	* o_offset = * o_size = 0;
	
    if (i_module)
    {
		if (i_dataSegmentIndex < i_module->numDataSegments)
		{
			M3DataSegment * segment = & i_module->dataSegments [i_dataSegmentIndex];
			
			* o_offset = segment->offset;
			* o_size   = segment->size;
		}
		else _throw (m3Err_indexOutOfBounds);
    }
    else _throw (m3Err_nullArgument);

    _catch: return result;
}


//-------------------------------------------------------------------------------------------------------------------------------
//  Wasm binary generation -- serializes a live M3Module back into a .wasm binary.
//-------------------------------------------------------------------------------------------------------------------------------

typedef struct M3Bytes
{
    u8 *    data;
    u32     size;
    u32     capacity;
}
M3Bytes;


static M3Result  Bytes_Grow  (M3Bytes * io_buf, u32 i_addBytes)
{
    M3Result result = m3Err_none;

    u32 needed = io_buf->size + i_addBytes;
    if (needed > io_buf->capacity)
    {
        u32 newCap = io_buf->capacity ? io_buf->capacity : 256;
        while (newCap < needed) newCap *= 2;

        u8 * newData = (u8 *) m3_Realloc ("wasm gen buffer", io_buf->data, newCap, io_buf->capacity);
        _throwifnull (newData);

        io_buf->data = newData;
        io_buf->capacity = newCap;
    }

    _catch: return result;
}


static M3Result  Bytes_PushByte  (M3Bytes * io_buf, u8 i_byte)
{
    M3Result result = m3Err_none;
_   (Bytes_Grow (io_buf, 1));
    io_buf->data [io_buf->size++] = i_byte;
    _catch: return result;
}


static M3Result  Bytes_Push  (M3Bytes * io_buf, const void * i_src, u32 i_numBytes)
{
    M3Result result = m3Err_none;
    if (i_numBytes)
    {
_       (Bytes_Grow (io_buf, i_numBytes));
        memcpy (io_buf->data + io_buf->size, i_src, i_numBytes);
        io_buf->size += i_numBytes;
    }
    _catch: return result;
}


static M3Result  Bytes_PushLEB_u32  (M3Bytes * io_buf, u32 i_value)
{
    M3Result result = m3Err_none;
    do
    {
        u8 byte = i_value & 0x7F;
        i_value >>= 7;
        if (i_value) byte |= 0x80;
_       (Bytes_PushByte (io_buf, byte));
    }
    while (i_value);

    _catch: return result;
}


static M3Result  Bytes_PushLEB_i32  (M3Bytes * io_buf, i32 i_value)
{
    M3Result result = m3Err_none;
    bool more = true;
    while (more)
    {
        u8 byte = i_value & 0x7F;
        i_value >>= 7;                                          // arithmetic shift preserves sign
        if ((i_value == 0 and not (byte & 0x40)) or (i_value == -1 and (byte & 0x40)))
            more = false;
        else
            byte |= 0x80;
_       (Bytes_PushByte (io_buf, byte));
    }

    _catch: return result;
}


static M3Result  Bytes_PushU32LE  (M3Bytes * io_buf, u32 i_value)             // fixed 4-byte little-endian (magic / version)
{
    M3Result result = m3Err_none;
_   (Bytes_PushByte (io_buf, (u8) (i_value      )));
_   (Bytes_PushByte (io_buf, (u8) (i_value >>  8)));
_   (Bytes_PushByte (io_buf, (u8) (i_value >> 16)));
_   (Bytes_PushByte (io_buf, (u8) (i_value >> 24)));
    _catch: return result;
}


static M3Result  Bytes_PushName  (M3Bytes * io_buf, cstr_t i_name)
{
    M3Result result = m3Err_none;
    u32 length = i_name ? (u32) strlen (i_name) : 0;
_   (Bytes_PushLEB_u32 (io_buf, length));
_   (Bytes_Push (io_buf, i_name, length));
    _catch: return result;
}


static M3Result  Bytes_PushSection  (M3Bytes * io_out, u8 i_sectionId, const M3Bytes * i_payload)
{
    M3Result result = m3Err_none;
    if (i_payload->size)                                        // omit empty sections
    {
_       (Bytes_PushByte (io_out, i_sectionId));
_       (Bytes_PushLEB_u32 (io_out, i_payload->size));
_       (Bytes_Push (io_out, i_payload->data, i_payload->size));
    }
    _catch: return result;
}


static u8  EncodeWasmType  (u8 i_m3Type)                        // i32->0x7F, i64->0x7E, f32->0x7D, f64->0x7C
{
    return (u8) (0x80 - i_m3Type);
}


// writes an init-expr that pushes a constant i32 and ends:  i32.const <value> ; end
static M3Result  Bytes_PushConstI32Expr  (M3Bytes * io_buf, i32 i_value)
{
    M3Result result = m3Err_none;
_   (Bytes_PushByte (io_buf, 0x41));                            // i32.const
_   (Bytes_PushLEB_i32 (io_buf, i_value));
_   (Bytes_PushByte (io_buf, 0x0B));                            // end
    _catch: return result;
}


M3Result  w3x_GenerateWasmModule  (IM3Module i_module, uint8_t ** o_wasmBytes, uint32_t * o_numWasmBytes)
{
	M3Result result = m3Err_none;
	
    M3Bytes out = { NULL, 0, 0 };                               // the assembled module
    M3Bytes sec = { NULL, 0, 0 };                               // reused per-section payload scratch
    M3Bytes sub = { NULL, 0, 0 };                               // nested scratch (name subsection content)

    _throwif (m3Err_nullArgument, not i_module or not o_wasmBytes or not o_numWasmBytes);

    * o_wasmBytes = NULL;
    * o_numWasmBytes = 0;

    u32 numDefined = i_module->numFunctions - i_module->numFuncImports;

    // -- header --
_   (Bytes_PushU32LE (& out, 0x6d736100));                      // "\0asm"
_   (Bytes_PushU32LE (& out, 1));                               // version 1

    // -- Type (1) --
    if (i_module->numFuncTypes)
    {
        sec.size = 0;
_       (Bytes_PushLEB_u32 (& sec, i_module->numFuncTypes));

        for (u32 i = 0; i < i_module->numFuncTypes; ++i)
        {
            IM3FuncType ft = i_module->funcTypes [i];           // M3FuncType.types is laid out [rets..., args...]
_           (Bytes_PushByte (& sec, 0x60));                     // func form

_           (Bytes_PushLEB_u32 (& sec, ft->numArgs));
            for (u32 a = 0; a < ft->numArgs; ++a)
_               (Bytes_PushByte (& sec, EncodeWasmType (ft->types [ft->numRets + a])));

_           (Bytes_PushLEB_u32 (& sec, ft->numRets));
            for (u32 r = 0; r < ft->numRets; ++r)
_               (Bytes_PushByte (& sec, EncodeWasmType (ft->types [r])));
        }

_       (Bytes_PushSection (& out, 1, & sec));
    }

    // -- Import (2) --
    {
        u32 numGlobalImports = 0;
        for (u32 i = 0; i < i_module->numGlobals; ++i)
            if (i_module->globals [i].imported) ++numGlobalImports;

        u32 numImports = i_module->numFuncImports + numGlobalImports + (i_module->memoryImported ? 1 : 0);

        if (numImports)
        {
            sec.size = 0;
_           (Bytes_PushLEB_u32 (& sec, numImports));

            // function imports occupy the low function indices
            for (u32 i = 0; i < i_module->numFuncImports; ++i)
            {
                IM3Function func = & i_module->functions [i];
                _throwif ("function import missing its type", not func->funcType);
                i32 typeIndex = Module_HasFuncType (i_module, func->funcType);
                _throwif ("function import missing its type", typeIndex < 0);

_               (Bytes_PushName (& sec, func->import.moduleUtf8));
_               (Bytes_PushName (& sec, func->import.fieldUtf8));
_               (Bytes_PushByte (& sec, d_externalKind_function));
_               (Bytes_PushLEB_u32 (& sec, (u32) typeIndex));
            }

            for (u32 i = 0; i < i_module->numGlobals; ++i)
            {
                M3Global * g = & i_module->globals [i];
                if (g->imported)
                {
_                   (Bytes_PushName (& sec, g->import.moduleUtf8));
_                   (Bytes_PushName (& sec, g->import.fieldUtf8));
_                   (Bytes_PushByte (& sec, d_externalKind_global));
_                   (Bytes_PushByte (& sec, EncodeWasmType (g->type)));
_                   (Bytes_PushByte (& sec, g->isMutable ? 1 : 0));
                }
            }

            if (i_module->memoryImported)
            {
                u8 flag = (i_module->memoryInfo.maxPages > 0) ? 1 : 0;
_               (Bytes_PushName (& sec, i_module->memoryImport.moduleUtf8));
_               (Bytes_PushName (& sec, i_module->memoryImport.fieldUtf8));
_               (Bytes_PushByte (& sec, d_externalKind_memory));
_               (Bytes_PushByte (& sec, flag));
_               (Bytes_PushLEB_u32 (& sec, i_module->memoryInfo.initPages));
                if (flag & 1)
_                   (Bytes_PushLEB_u32 (& sec, i_module->memoryInfo.maxPages));
            }

_           (Bytes_PushSection (& out, 2, & sec));
        }
    }

    // -- Function (3) --
    if (numDefined)
    {
        sec.size = 0;
_       (Bytes_PushLEB_u32 (& sec, numDefined));

        for (u32 i = i_module->numFuncImports; i < i_module->numFunctions; ++i)
        {
            _throwif ("function missing its type", not i_module->functions [i].funcType);
            i32 typeIndex = Module_HasFuncType (i_module, i_module->functions [i].funcType);
            _throwif ("function missing its type", typeIndex < 0);
_           (Bytes_PushLEB_u32 (& sec, (u32) typeIndex));
        }

_       (Bytes_PushSection (& out, 3, & sec));
    }

    // -- Table (4) --
    // wasm3 discards the table's declared limits (ParseType_Table is a no-op), so reconstruct a funcref
    // table sized to hold the live table0 (covers original elements + any injected entries).  Required so
    // call_indirect and the element segment have a table to reference.
    if (i_module->table0Size or i_module->numElementSegments or i_module->table0ExportName)
    {
        sec.size = 0;
_       (Bytes_PushLEB_u32 (& sec, 1));                         // one table
_       (Bytes_PushByte    (& sec, 0x70));                      // funcref
_       (Bytes_PushByte    (& sec, 0));                         // limits: no maximum
_       (Bytes_PushLEB_u32 (& sec, i_module->table0Size));      // minimum = element count
_       (Bytes_PushSection (& out, 4, & sec));
    }

    // -- Memory (5) --
    if (not i_module->memoryImported and
        (i_module->memoryInfo.initPages or i_module->numDataSegments or i_module->memoryExportName))
    {
        u8 flag = (i_module->memoryInfo.maxPages > 0) ? 1 : 0;
        sec.size = 0;
_       (Bytes_PushLEB_u32		(& sec, 1));                         // one memory
_       (Bytes_PushByte			(& sec, flag));
_       (Bytes_PushLEB_u32		(& sec, i_module->memoryInfo.initPages));
        if (flag & 1)
_           (Bytes_PushLEB_u32	(& sec, i_module->memoryInfo.maxPages));

_       (Bytes_PushSection		(& out, 5, & sec));
    }

    // -- Global (6) --  defined (non-imported) globals
    {
        u32 numDefinedGlobals = 0;
		
        for (u32 i = 0; i < i_module->numGlobals; ++i)
            if (not i_module->globals [i].imported) ++numDefinedGlobals;

        if (numDefinedGlobals)
        {
            sec.size = 0;
_           (Bytes_PushLEB_u32 (& sec, numDefinedGlobals));

            for (u32 i = 0; i < i_module->numGlobals; ++i)
            {
                M3Global * g = & i_module->globals [i];
                if (not g->imported)
                {
_                   (Bytes_PushByte	(& sec, EncodeWasmType (g->type)));
_                   (Bytes_PushByte (& sec, g->isMutable ? 1 : 0));
					
					if (g->wasApiModified)
					{
_						(Bytes_PushConstI32Expr (& sec, g->i32Value));
					}
					else
					{
						_throwif ("global missing init expression", not g->initExpr or g->initExprSize == 0);
_                   	(Bytes_Push		(& sec, g->initExpr, g->initExprSize));     // verbatim wasm, includes 'end'
					}
                }
            }

_           (Bytes_PushSection (& out, 6, & sec));
        }
    }

    // -- Export (7) --
    {
        u32 numExports = (i_module->memoryExportName ? 1 : 0) + (i_module->table0ExportName ? 1 : 0);
        
		for (u32 i = 0; i < i_module->numFunctions; ++i)
            if (i_module->functions [i].export_name) ++numExports;
		
        for (u32 i = 0; i < i_module->numGlobals; ++i)
            if (i_module->globals [i].name) ++numExports;

        if (numExports)
        {
            sec.size = 0;
_           (Bytes_PushLEB_u32 (& sec, numExports));

            for (u32 i = 0; i < i_module->numFunctions; ++i)
            {
                IM3Function func = & i_module->functions [i];
                if (func->export_name)
                {
_                   (Bytes_PushName		(& sec, func->export_name));
_                   (Bytes_PushByte		(& sec, d_externalKind_function));
_                   (Bytes_PushLEB_u32	(& sec, i));
                }
            }

            for (u32 i = 0; i < i_module->numGlobals; ++i)
            {
                M3Global * g = & i_module->globals [i];
                if (g->name)
                {
_                   (Bytes_PushName		(& sec, g->name));
_                   (Bytes_PushByte		(& sec, d_externalKind_global));
_                   (Bytes_PushLEB_u32	(& sec, i));
                }
            }

            if (i_module->memoryExportName)
            {
_               (Bytes_PushName		(& sec, i_module->memoryExportName));
_               (Bytes_PushByte		(& sec, d_externalKind_memory));
_               (Bytes_PushLEB_u32	(& sec, 0));
            }

            if (i_module->table0ExportName)
            {
_               (Bytes_PushName (& sec, i_module->table0ExportName));
_               (Bytes_PushByte		(& sec, d_externalKind_table));
_               (Bytes_PushLEB_u32	(& sec, 0));
            }

_           (Bytes_PushSection (& out, 7, & sec));
        }
    }

    // -- Start (8) --
    if (i_module->startFunction >= 0)
    {
        sec.size = 0;
_       (Bytes_PushLEB_u32 (& sec, (u32) i_module->startFunction));
_       (Bytes_PushSection (& out, 8, & sec));
    }

    // -- Element (9) --
	if (i_module->table0 and i_module->table0Size)
	{
		sec.size = 0;
_      	(Bytes_PushLEB_u32		(& sec, 1));                     // one element segment
_       (Bytes_PushLEB_u32		(& sec, 0));                     // table index 0
_		(Bytes_PushConstI32Expr	(& sec, 0));   		             // offset 0
_		(Bytes_PushLEB_u32		(& sec, i_module->table0Size));
		
		for (u32 i = 0; i < i_module->table0Size; ++i)
		{
			IM3Function function = i_module->table0 [i];
			u32 index = (function) ? function->index : 0;
			
_			(Bytes_PushLEB_u32	(& sec, index));
		}
		
_		(Bytes_PushSection		(& out, 9, & sec));
	}
		
    // -- Code (10) --  each function's stored wasm already includes its LEB size prefix + locals + body
    if (numDefined)
    {
        sec.size = 0;
_       (Bytes_PushLEB_u32 (& sec, numDefined));

        for (u32 i = i_module->numFuncImports; i < i_module->numFunctions; ++i)
        {
            IM3Function func = & i_module->functions [i];

			if (func->wasm and func->wasmEnd)
			{
_	           (Bytes_Push (& sec, func->wasm, (u32) (func->wasmEnd - func->wasm)));
			}
			else
			{
				// dummy/empty function
_				(Bytes_PushLEB_u32	(& sec, 2));
_				(Bytes_PushLEB_u32	(& sec, 0)); 	// 0 locals
_				(Bytes_PushByte		(& sec, 0x0B));	// end opcode
			}
        }

_       (Bytes_PushSection (& out, 10, & sec));
    }

    // -- Data (11) --  payload snapshotted from the runtime's live linear memory
    if (i_module->numDataSegments)
    {
        _throwif ("module must be loaded into a runtime to snapshot data", not i_module->runtime);
        M3MemoryHeader * mem = i_module->runtime->memory.mallocated;
        _throwif ("runtime has no linear memory", not mem);

        u8 * memData = m3MemData (mem);
        size_t memLength = mem->length;

        sec.size = 0;
_       (Bytes_PushLEB_u32 (& sec, i_module->numDataSegments));

        for (u32 i = 0; i < i_module->numDataSegments; ++i)
        {
            M3DataSegment * segment = & i_module->dataSegments [i];     // union now holds resolved 'offset'
            i32 offset = segment->offset;

            _throwif ("data segment out of bounds", offset < 0 or (size_t) offset + segment->size > memLength);

_           (Bytes_PushLEB_u32 (& sec, 0));                            // memory index 0
_           (Bytes_PushConstI32Expr (& sec, offset));
_           (Bytes_PushLEB_u32 (& sec, segment->size));
_           (Bytes_Push (& sec, memData + offset, segment->size));
        }

_       (Bytes_PushSection (& out, 11, & sec));
    }

    // -- Custom "name" (0) --  function names, mirroring ParseSection_Name (nameType 1)
    {
        u32 numNames = 0;
        for (u32 i = 0; i < i_module->numFunctions; ++i)
            if (i_module->functions [i].numNames and i_module->functions [i].names [0]) ++numNames;

        if (numNames)
        {
            // build the nameType-1 subsection content: count + (funcIndex, name)*
            sub.size = 0;
_           (Bytes_PushLEB_u32 (& sub, numNames));
            for (u32 i = 0; i < i_module->numFunctions; ++i)
            {
                IM3Function func = & i_module->functions [i];
                if (func->numNames and func->names [0])
                {
_                   (Bytes_PushLEB_u32 (& sub, i));
_                   (Bytes_PushName (& sub, func->names [0]));
                }
            }

            // wrap it in the custom section payload: "name" + subsection (id, size, content)
            sec.size = 0;
_           (Bytes_PushName (& sec, "name"));
_           (Bytes_PushByte (& sec, 1));                        // nameType 1 = function names
_           (Bytes_PushLEB_u32 (& sec, sub.size));
_           (Bytes_Push (& sec, sub.data, sub.size));

_           (Bytes_PushSection (& out, 0, & sec));
        }
    }

    * o_wasmBytes = out.data;
    * o_numWasmBytes = out.size;
    out.data = NULL;                                            // ownership transferred to caller

    _catch:

    m3_Free (sub.data);
    m3_Free (sec.data);
    m3_Free (out.data);                                         // no-op on success (nulled above)

    return result;
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
