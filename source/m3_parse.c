//
//  m3_parse.c
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_compile.h"
#include "m3_exception.h"
#include "m3_info.h"


M3Result  ParseType_Table  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numTables;
_   (ReadLEB_u32 (& numTables, & i_bytes, i_end));                       m3log (parse, "** Table [%d]", numTables);

    // MVP: at most one table, counting any that was already imported
    _throwif (m3Err_wasmMalformed, numTables > 1);
    _throwif (m3Err_wasmMalformed, numTables and io_module->hasTable);

    for (u32 i = 0; i < numTables; ++i)
    {
        u8 elemType;
_       (Read_u8 (& elemType, & i_bytes, i_end));
        // Spec: element type must be funcref (0x70)
        _throwif (m3Err_wasmMalformed, elemType != 0x70);

        u8 flag;
_       (ReadLEB_u7 (& flag, & i_bytes, i_end));
        u32 initSize;
_       (ReadLEB_u32 (& initSize, & i_bytes, i_end));
        if (flag & 1) {
            u32 maxSize;
_           (ReadLEB_u32 (& maxSize, & i_bytes, i_end));
            _throwif (m3Err_wasmMalformed, maxSize < initSize);
        }
        io_module->hasTable = true;
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch: return result;
}


M3Result  ParseType_Memory  (M3MemoryInfo * o_memory, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u8 flag;

_   (ReadLEB_u7 (& flag, io_bytes, i_end));                   // really a u1
_   (ReadLEB_u32 (& o_memory->initPages, io_bytes, i_end));

    o_memory->maxPages = 0;
    if (flag & (1u << 0))
    {
_       (ReadLEB_u32 (& o_memory->maxPages, io_bytes, i_end));

        // Spec: memory limits validation - max must not be less than init
        _throwif (m3Err_wasmMalformed, o_memory->maxPages < o_memory->initPages);
    }

    o_memory->pageSize = 0;
    if (flag & (1u << 3)) {
        u32 logPageSize;
_       (ReadLEB_u32 (& logPageSize, io_bytes, i_end));
        o_memory->pageSize = 1u << logPageSize;
    }

    // Spec: memory limits must be valid within range 2^16 (65536 pages)
    // Only enforce for standard page size (no custom page size flag)
    if (!(flag & (1u << 3)))
    {
        _throwif (m3Err_wasmMalformed, o_memory->initPages > 65536);
        if (flag & (1u << 0))
            _throwif (m3Err_wasmMalformed, o_memory->maxPages > 65536);
    }

    _catch: return result;
}


M3Result  ParseSection_Type  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    IM3FuncType ftype = NULL;

_try {
    u32 numTypes;
_   (ReadLEB_u32 (& numTypes, & i_bytes, i_end));                                   m3log (parse, "** Type [%d]", numTypes);

    _throwif("too many types", numTypes > d_m3MaxSaneTypesCount);

    if (numTypes)
    {
        // table of IM3FuncType (that point to the actual M3FuncType struct in the Environment)
        io_module->funcTypes = m3_AllocArray (IM3FuncType, numTypes);
        _throwifnull (io_module->funcTypes);
        io_module->numFuncTypes = numTypes;

        for (u32 i = 0; i < numTypes; ++i)
        {
            i8 form;
_           (ReadLEB_i7 (& form, & i_bytes, i_end));
            _throwif (m3Err_wasmMalformed, form != -32); // for Wasm MVP

            u32 numArgs;
_           (ReadLEB_u32 (& numArgs, & i_bytes, i_end));

            _throwif (m3Err_tooManyArgsRets, numArgs > d_m3MaxSaneFunctionArgRetCount);
#if defined(M3_COMPILER_MSVC)
            u8 argTypes [d_m3MaxSaneFunctionArgRetCount];
#else
            u8 argTypes[numArgs+1]; // make ubsan happy
#endif
            for (u32 a = 0; a < numArgs; ++a)
            {
                i8 wasmType;
                u8 argType;
_               (ReadLEB_i7 (& wasmType, & i_bytes, i_end));
_               (NormalizeType (& argType, wasmType));

                argTypes[a] = argType;
            }

            u32 numRets;
_           (ReadLEB_u32 (& numRets, & i_bytes, i_end));
            _throwif (m3Err_tooManyArgsRets, (u64)(numRets) + numArgs > d_m3MaxSaneFunctionArgRetCount);

_           (AllocFuncType (& ftype, numRets + numArgs));
            ftype->numArgs = numArgs;
            ftype->numRets = numRets;

            for (u32 r = 0; r < numRets; ++r)
            {
                i8 wasmType;
                u8 retType;
_               (ReadLEB_i7 (& wasmType, & i_bytes, i_end));
_               (NormalizeType (& retType, wasmType));

                ftype->types[r] = retType;
            }
            memcpy (ftype->types + numRets, argTypes, numArgs);                                 m3log (parse, "    type %2d: %s", i, SPrintFuncTypeSignature (ftype));

            Environment_AddFuncType (io_module->environment, & ftype);
            io_module->funcTypes [i] = ftype;
            ftype = NULL; // ownership transferred to environment
        }
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

} _catch:

    if (result)
    {
        m3_Free (ftype);
        // FIX: M3FuncTypes in the table are leaked
        m3_Free (io_module->funcTypes);
        io_module->numFuncTypes = 0;
    }

    return result;
}


M3Result  ParseSection_Function  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numFunctions;
_   (ReadLEB_u32 (& numFunctions, & i_bytes, i_end));                               m3log (parse, "** Function [%d]", numFunctions);

    _throwif("too many functions", numFunctions > d_m3MaxSaneFunctionsCount);

_   (Module_PreallocFunctions(io_module, io_module->numFunctions + numFunctions));

    for (u32 i = 0; i < numFunctions; ++i)
    {
        u32 funcTypeIndex;
_       (ReadLEB_u32 (& funcTypeIndex, & i_bytes, i_end));

_       (Module_AddFunction (io_module, funcTypeIndex, NULL /* import info */));
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch: return result;
}


M3Result  ParseSection_Import  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    M3ImportInfo import = { NULL, NULL }, clearImport = { NULL, NULL };

    u32 numImports;
_   (ReadLEB_u32 (& numImports, & i_bytes, i_end));                                 m3log (parse, "** Import [%d]", numImports);

    _throwif("too many imports", numImports > d_m3MaxSaneImportsCount);

    // Most imports are functions, so we won't waste much space anyway (if any)
_   (Module_PreallocFunctions(io_module, numImports));

    for (u32 i = 0; i < numImports; ++i)
    {
        u8 importKind;

_       (Read_utf8 (& import.moduleUtf8, & i_bytes, i_end));
_       (Read_utf8 (& import.fieldUtf8, & i_bytes, i_end));
_       (Read_u8 (& importKind, & i_bytes, i_end));                                 m3log (parse, "    kind: %d '%s.%s' ",
                                                                                                (u32) importKind, import.moduleUtf8, import.fieldUtf8);
        switch (importKind)
        {
            case d_externalKind_function:
            {
                u32 typeIndex;
_               (ReadLEB_u32 (& typeIndex, & i_bytes, i_end))

_               (Module_AddFunction (io_module, typeIndex, & import))
                import = clearImport;

                io_module->numFuncImports++;
            }
            break;

            case d_externalKind_table:
            {
                // Parse and validate table type (elem type + limits)
                u8 elemType;
_               (Read_u8 (& elemType, & i_bytes, i_end));
                _throwif (m3Err_wasmMalformed, elemType != 0x70); // must be funcref
                u8 flag;
_               (ReadLEB_u7 (& flag, & i_bytes, i_end));
                u32 initSize;
_               (ReadLEB_u32 (& initSize, & i_bytes, i_end));
                if (flag & 1) {
                    u32 maxSize;
_                   (ReadLEB_u32 (& maxSize, & i_bytes, i_end));
                }
                io_module->hasTable = true;
            }
            break;

            case d_externalKind_memory:
            {
_               (ParseType_Memory (& io_module->memoryInfo, & i_bytes, i_end));
                io_module->memoryImported = true;
                io_module->memoryImport = import;
                import = clearImport;
            }
            break;

            case d_externalKind_global:
            {
                i8 waType;
                u8 type, isMutable;

_               (ReadLEB_i7 (& waType, & i_bytes, i_end));
_               (NormalizeType (& type, waType));
_               (ReadLEB_u7 (& isMutable, & i_bytes, i_end));                     m3log (parse, "     global: %s mutable=%d", c_waTypes [type], (u32) isMutable);
                _throwif (m3Err_wasmMalformed, isMutable > 1);

                IM3Global global;
_               (Module_AddGlobal (io_module, & global, type, isMutable, true /* isImport */));
                global->import = import;
                import = clearImport;
            }
            break;

            default:
                _throw (m3Err_wasmMalformed);
        }

        FreeImportInfo (& import);
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch:

    FreeImportInfo (& import);

    return result;
}


M3Result  ParseSection_Export  (IM3Module io_module, bytes_t i_bytes, cbytes_t  i_end)
{
    M3Result result = m3Err_none;
    const char * utf8 = NULL;
#if d_m3EnableValidation
    // We store name pointers + lengths to handle embedded NUL bytes correctly
    typedef struct { const u8 * ptr; u16 len; } ExportName;
    ExportName * exportNames = NULL;
#endif

    u32 numExports;
_   (ReadLEB_u32 (& numExports, & i_bytes, i_end));                                 m3log (parse, "** Export [%d]", numExports);

    _throwif("too many exports", numExports > d_m3MaxSaneExportsCount);

#if d_m3EnableValidation
    // Spec: all export names must be different
    if (numExports > 1)
    {
        exportNames = (ExportName *) m3_Malloc ("exportNames", sizeof(ExportName) * numExports);
    }
#endif

    for (u32 i = 0; i < numExports; ++i)
    {
        u8 exportKind;
        u32 index;

        // Read name length and remember raw position for uniqueness check
#if d_m3EnableValidation
        const u8 * nameStart = i_bytes;
        u32 nameLen = 0;
        {
            bytes_t tmp = i_bytes;
            M3Result rl = ReadLEB_u32 (& nameLen, & tmp, i_end);
            if (rl) { m3_Free(exportNames); _throw(rl); }
            nameStart = tmp; // points to the raw name bytes
        }
#endif

_       (Read_utf8 (& utf8, & i_bytes, i_end));
_       (Read_u8 (& exportKind, & i_bytes, i_end));
_       (ReadLEB_u32 (& index, & i_bytes, i_end));                                  m3log (parse, "    index: %3d; kind: %d; export: '%s'; ", index, (u32) exportKind, utf8);

#if d_m3EnableValidation
        if (exportNames)
        {
            for (u32 j = 0; j < i; ++j)
            {
                if (exportNames[j].len == nameLen &&
                    memcmp (exportNames[j].ptr, nameStart, nameLen) == 0)
                {
                    m3_Free (exportNames);
                    _throw (m3Err_wasmMalformed);  // duplicate export name
                }
            }
            exportNames[i].ptr = nameStart;
            exportNames[i].len = (u16)nameLen;
        }
#endif

        if (exportKind == d_externalKind_function)
        {
            _throwif(m3Err_wasmMalformed, index >= io_module->numFunctions);
            IM3Function func = &(io_module->functions [index]);
            if (func->numNames < d_m3MaxDuplicateFunctionImpl)
            {
                func->names[func->numNames++] = utf8;
                func->export_name = utf8;
                utf8 = NULL; // ownership transferred to M3Function
            }
        }
        else if (exportKind == d_externalKind_global)
        {
            _throwif(m3Err_wasmMalformed, index >= io_module->numGlobals);
            IM3Global global = &(io_module->globals [index]);
            m3_Free (global->name);
            global->name = utf8;
            utf8 = NULL; // ownership transferred to M3Global
        }
        else if (exportKind == d_externalKind_memory)
        {
            _throwif(m3Err_wasmMalformed, index != 0);
            _throwif(m3Err_wasmMalformed, not (io_module->memoryImported or io_module->memoryDeclared));
            m3_Free (io_module->memoryExportName);
            io_module->memoryExportName = utf8;
            utf8 = NULL; // ownership transferred to M3Module
        }
        else if (exportKind == d_externalKind_table)
        {
            _throwif(m3Err_wasmMalformed, index != 0);
            _throwif(m3Err_wasmMalformed, not io_module->hasTable);
            m3_Free (io_module->table0ExportName);
            io_module->table0ExportName = utf8;
            utf8 = NULL; // ownership transferred to M3Module
        }

        m3_Free (utf8);
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

_catch:
    m3_Free (utf8);
#if d_m3EnableValidation
    m3_Free (exportNames);
#endif
    return result;
}


M3Result  ParseSection_Start  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 startFuncIndex;
_   (ReadLEB_u32 (& startFuncIndex, & i_bytes, i_end));                               m3log (parse, "** Start Function: %d", startFuncIndex);

    if (startFuncIndex < io_module->numFunctions)
    {
        // Spec: start function type must be [] -> []
        IM3Function func = & io_module->functions [startFuncIndex];
        if (func->funcType)
        {
            _throwif (m3Err_wasmMalformed,
                      func->funcType->numArgs != 0 || func->funcType->numRets != 0);
        }

        io_module->startFunction = startFuncIndex;
    }
    else result = "start function index out of bounds";

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch: return result;
}


M3Result  Parse_InitExpr  (M3Module * io_module, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    // this doesn't generate code pages. just walks the wasm bytecode to find the end

#if defined(d_m3PreferStaticAlloc)
    static M3Compilation compilation;
#else
    M3Compilation compilation;
#endif
    compilation = (M3Compilation){ .runtime = NULL, .module = io_module, .wasm = * io_bytes, .wasmEnd = i_end, .isInitExpr = true };

    result = CompileBlockStatements (& compilation);

    * io_bytes = compilation.wasm;

    return result;
}


M3Result  ParseSection_Element  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numSegments;
    bytes_t pos;
_   (ReadLEB_u32 (& numSegments, & i_bytes, i_end));                         m3log (parse, "** Element [%d]", numSegments);

    _throwif ("too many element segments", numSegments > d_m3MaxSaneElementSegments);

    // Element segments need a table to populate
    _throwif (m3Err_wasmMalformed, numSegments and not io_module->hasTable);

    io_module->elementSection = i_bytes;
    io_module->elementSectionEnd = i_end;
    io_module->numElementSegments = numSegments;

    // Walk the section to validate structure and detect section size mismatch.
    // The actual element initialization happens later in InitElements.
    pos = i_bytes;
    for (u32 i = 0; i < numSegments; ++i)
    {
        u32 tableIndex;
_       (ReadLEB_u32 (& tableIndex, & pos, i_end));

        // Walk the init expression (offset) to find its end
_       (Parse_InitExpr (io_module, & pos, i_end));

        u32 numElements;
_       (ReadLEB_u32 (& numElements, & pos, i_end));

        for (u32 e = 0; e < numElements; ++e)
        {
            u32 funcIndex;
_           (ReadLEB_u32 (& funcIndex, & pos, i_end));
        }
    }

    _throwif (m3Err_wasmMalformed, pos != i_end);           // section size mismatch

    _catch: return result;
}


M3Result  ParseSection_Code  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result;

    u32 numFunctions;
_   (ReadLEB_u32 (& numFunctions, & i_bytes, i_end));                               m3log (parse, "** Code [%d]", numFunctions);

    if (numFunctions != io_module->numFunctions - io_module->numFuncImports)
    {
        _throw ("mismatched function count in code section");
    }

    for (u32 f = 0; f < numFunctions; ++f)
    {
        const u8 * start = i_bytes;

        u32 size;
_       (ReadLEB_u32 (& size, & i_bytes, i_end));

        if (size)
        {
            const u8 * ptr = i_bytes;
            i_bytes += size;

            if (i_bytes <= i_end)
            {
                /*
                u32 numLocalBlocks;
_               (ReadLEB_u32 (& numLocalBlocks, & ptr, i_end));                                      m3log (parse, "    code size: %-4d", size);

                u32 numLocals = 0;

                for (u32 l = 0; l < numLocalBlocks; ++l)
                {
                    u32 varCount;
                    i8 wasmType;
                    u8 normalType;

_                   (ReadLEB_u32 (& varCount, & ptr, i_end));
_                   (ReadLEB_i7 (& wasmType, & ptr, i_end));
_                   (NormalizeType (& normalType, wasmType));

                    numLocals += varCount;                                                      m3log (parse, "      %2d locals; type: '%s'", varCount, c_waTypes [normalType]);
                }
                 */

                IM3Function func = Module_GetFunction (io_module, f + io_module->numFuncImports);

                func->module = io_module;
                func->wasm = start;
                func->wasmEnd = i_bytes;
                //func->ownsWasmCode = io_module->hasWasmCodeCopy;
//                func->numLocals = numLocals;
            }
            else _throw (m3Err_wasmSectionOverrun);
        }
    }

    _catch:

    if (not result and i_bytes != i_end)
        result = m3Err_wasmSectionUnderrun;

    return result;
}


M3Result  ParseSection_Data  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numDataSegments;
_   (ReadLEB_u32 (& numDataSegments, & i_bytes, i_end));                            m3log (parse, "** Data [%d]", numDataSegments);

    _throwif("too many data segments", numDataSegments > d_m3MaxSaneDataSegments);

    io_module->dataSegments = m3_AllocArray (M3DataSegment, numDataSegments);
    _throwifnull(io_module->dataSegments);
    io_module->numDataSegments = numDataSegments;

    for (u32 i = 0; i < numDataSegments; ++i)
    {
        M3DataSegment * segment = & io_module->dataSegments [i];

_       (ReadLEB_u32 (& segment->memoryRegion, & i_bytes, i_end));

        // Spec: MVP only supports memory index 0, and it has to exist
        _throwif (m3Err_wasmMalformed, segment->memoryRegion != 0);
        _throwif (m3Err_wasmMalformed, not (io_module->memoryImported or io_module->memoryDeclared));

        segment->initExpr = i_bytes;
_       (Parse_InitExpr (io_module, & i_bytes, i_end));
        segment->initExprSize = (u32) (i_bytes - segment->initExpr);

        _throwif (m3Err_wasmMissingInitExpr, segment->initExprSize <= 1);

_       (ReadLEB_u32 (& segment->size, & i_bytes, i_end));
        segment->data = i_bytes;                                                    m3log (parse, "    segment [%u]  memory: %u;  expr-size: %d;  size: %d",
                                                                                       i, segment->memoryRegion, segment->initExprSize, segment->size);
        i_bytes += segment->size;

        _throwif("data segment underflow", i_bytes > i_end);
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch:

    return result;
}


M3Result  ParseSection_Memory  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    // TODO: MVP; assert no memory imported

    u32 numMemories;
_   (ReadLEB_u32 (& numMemories, & i_bytes, i_end));                             m3log (parse, "** Memory [%d]", numMemories);

    _throwif (m3Err_tooManyMemorySections, numMemories > 1);

    if (numMemories)
    {
_       (ParseType_Memory (& io_module->memoryInfo, & i_bytes, i_end));
        io_module->memoryDeclared = true;
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch: return result;
}


M3Result  ParseSection_Global  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numGlobals;
_   (ReadLEB_u32 (& numGlobals, & i_bytes, i_end));                                 m3log (parse, "** Global [%d]", numGlobals);

    _throwif("too many globals", numGlobals > d_m3MaxSaneGlobalsCount);

    for (u32 i = 0; i < numGlobals; ++i)
    {
        i8 waType;
        u8 type, isMutable;

_       (ReadLEB_i7 (& waType, & i_bytes, i_end));
_       (NormalizeType (& type, waType));
_       (ReadLEB_u7 (& isMutable, & i_bytes, i_end));                                 m3log (parse, "    global: [%d] %s mutable: %d", i, c_waTypes [type],   (u32) isMutable);
        _throwif (m3Err_wasmMalformed, isMutable > 1);

        IM3Global global;
_       (Module_AddGlobal (io_module, & global, type, isMutable, false /* isImport */));

        global->initExpr = i_bytes;
_       (Parse_InitExpr (io_module, & i_bytes, i_end));
        global->initExprSize = (u32) (i_bytes - global->initExpr);

        _throwif (m3Err_wasmMissingInitExpr, global->initExprSize <= 1);
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch: return result;
}


M3Result  ParseSection_Name  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    cstr_t name;

    while (i_bytes < i_end)
    {
        u8 nameType;
        u32 payloadLength;

_       (ReadLEB_u7 (& nameType, & i_bytes, i_end));
_       (ReadLEB_u32 (& payloadLength, & i_bytes, i_end));

        bytes_t start = i_bytes;
        if (nameType == 1)
        {
            u32 numNames;
_           (ReadLEB_u32 (& numNames, & i_bytes, i_end));

            _throwif("too many names", numNames > d_m3MaxSaneFunctionsCount);

            for (u32 i = 0; i < numNames; ++i)
            {
                u32 index;
_               (ReadLEB_u32 (& index, & i_bytes, i_end));
_               (Read_utf8 (& name, & i_bytes, i_end));

                if (index < io_module->numFunctions)
                {
                    IM3Function func = &(io_module->functions [index]);
                    if (func->numNames == 0)
                    {
                        func->names[0] = name;        m3log (parse, "    naming function%5d:  %s", index, name);
                        func->numNames = 1;
                        name = NULL; // transfer ownership
                    }
//                          else m3log (parse, "prenamed: %s", io_module->functions [index].name);
                }

                m3_Free (name);
            }
        }

        i_bytes = start + payloadLength;
    }

    _catch: return result;
}


M3Result  ParseSection_Custom  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result;

    cstr_t name;
_   (Read_utf8 (& name, & i_bytes, i_end));
                                                                                    m3log (parse, "** Custom: '%s'", name);
    if (strcmp (name, "name") == 0) {
_       (ParseSection_Name(io_module, i_bytes, i_end));
    } else if (io_module->environment->customSectionHandler) {
_       (io_module->environment->customSectionHandler(io_module, name, i_bytes, i_end));
    }

    m3_Free (name);

    _catch: return result;
}


M3Result  ParseModuleSection  (M3Module * o_module, u8 i_sectionType, bytes_t i_bytes, u32 i_numBytes)
{
    M3Result result = m3Err_none;

    typedef M3Result (* M3Parser) (M3Module *, bytes_t, cbytes_t);

    static M3Parser s_parsers [] =
    {
        ParseSection_Custom,    // 0
        ParseSection_Type,      // 1
        ParseSection_Import,    // 2
        ParseSection_Function,  // 3
        ParseType_Table,        // 4
        ParseSection_Memory,    // 5
        ParseSection_Global,    // 6
        ParseSection_Export,    // 7
        ParseSection_Start,     // 8
        ParseSection_Element,   // 9
        ParseSection_Code,      // 10
        ParseSection_Data,      // 11
        NULL,                   // 12: TODO DataCount
    };

    M3Parser parser = NULL;

    if (i_sectionType <= 12)
        parser = s_parsers [i_sectionType];

    if (parser)
    {
        cbytes_t end = i_bytes + i_numBytes;
        result = parser (o_module, i_bytes, end);
    }
    else
    {
        m3log (parse, " skipped section type: %d", (u32) i_sectionType);
    }

    return result;
}


M3Result  m3_ParseModule  (IM3Environment i_environment, IM3Module * o_module, cbytes_t i_bytes, u32 i_numBytes)
{
    IM3Module module;                                                               m3log (parse, "load module: %d bytes", i_numBytes);
_try {
    module = m3_AllocStruct (M3Module);
    _throwifnull (module);
    module->name = ".unnamed";                                                      m3log (parse, "load module: %d bytes", i_numBytes);
    module->startFunction = -1;
    //module->hasWasmCodeCopy = false;
    module->environment = i_environment;

    const u8 * pos = i_bytes;
    const u8 * end = pos + i_numBytes;

    module->wasmStart = pos;
    module->wasmEnd = end;

    u32 magic, version;
_   (Read_u32 (& magic, & pos, end));
_   (Read_u32 (& version, & pos, end));

    _throwif (m3Err_wasmMalformed, magic != 0x6d736100);
    _throwif (m3Err_incompatibleWasmVersion, version != 1);

    static const u8 sectionsOrder[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 10, 11, 0 }; // 0 is a placeholder
    u8 expectedSection = 0;

    while (pos < end)
    {
        u8 section;
_       (ReadLEB_u7 (& section, & pos, end));

        if (section != 0) {
            // Ensure sections appear only once and in order
            while (sectionsOrder[expectedSection++] != section) {
                _throwif(m3Err_misorderedWasmSection, expectedSection >= 12);
            }
        }

        u32 sectionLength;
_       (ReadLEB_u32 (& sectionLength, & pos, end));
        _throwif(m3Err_wasmMalformed, pos + sectionLength > end);

_       (ParseModuleSection (module, section, pos, sectionLength));

        pos += sectionLength;
    }

    // Spec: if a function section exists, a code section must also exist with
    // matching count (and vice versa). ParseSection_Code checks the other
    // direction; this covers the case where the code section is missing entirely.
    if (module->numFunctions > module->numFuncImports)
    {
        IM3Function firstNonImport = & module->functions [module->numFuncImports];
        _throwif (m3Err_wasmMalformed, firstNonImport->wasm == NULL);
    }

} _catch:

    if (result)
    {
        m3_FreeModule (module);
        module = NULL;
    }

    * o_module = module;

    return result;
}
