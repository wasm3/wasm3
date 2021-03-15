//
//  m3_parse.c
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_compile.h"
#include "m3_exec.h"
#include "m3_exception.h"
#include "m3_info.h"


M3Result  ParseType_Table  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    return result;
}


M3Result  ParseType_Memory  (M3MemoryInfo * o_memory, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u8 flag;

_   (ReadLEB_u7 (& flag, io_bytes, i_end));                   // really a u1
_   (ReadLEB_u32 (& o_memory->initPages, io_bytes, i_end));

    o_memory->maxPages = 0;
    if (flag)
_       (ReadLEB_u32 (& o_memory->maxPages, io_bytes, i_end));

    _catch: return result;
}


M3Result  ParseSection_Type  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;
    IM3FuncType ftype = NULL;

_try {
    u32 numTypes;
_   (ReadLEB_u32 (& numTypes, & i_bytes, i_end));                                   m3log (parse, "** Type [%d]", numTypes);

    if (numTypes)
    {
        // table of IM3FuncType (that point to the actual M3FuncType struct in the Environment)
_       (m3Alloc (& io_module->funcTypes, IM3FuncType, numTypes));
        io_module->numFuncTypes = numTypes;

        for (u32 i = 0; i < numTypes; ++i)
        {
            i8 form;
_           (ReadLEB_i7 (& form, & i_bytes, i_end));
            _throwif (m3Err_wasmMalformed, form != -32); // for WA MVP

            u32 numArgs;
_           (ReadLEB_u32 (& numArgs, & i_bytes, i_end));

            _throwif ("insane argument count", numArgs > d_m3MaxSaneFunctionArgCount);

#if defined(M3_COMPILER_MSVC)
            u8 argTypes[d_m3MaxSaneFunctionArgCount];
#else
            u8 argTypes[numArgs];
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

            _throwif ("insane returns count", numRets > d_m3MaxSaneFunctionArgCount);

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
            memcpy(ftype->types + numRets, argTypes, numArgs);                                m3log (parse, "    type %2d: %s", i, SPrintFuncTypeSignature (ftype));

            Environment_AddFuncType (io_module->environment, & ftype);
            io_module->funcTypes [i] = ftype;
        }
    }

} _catch:

    if (result)
    {
        m3Free (ftype);
        m3Free (io_module->funcTypes);
        io_module->numFuncTypes = 0;
    }

    return result;
}


M3Result  ParseSection_Function  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numFunctions;
_   (ReadLEB_u32 (& numFunctions, & i_bytes, i_end));                               m3log (parse, "** Function [%d]", numFunctions);

    for (u32 i = 0; i < numFunctions; ++i)
    {
        u32 funcTypeIndex;
_       (ReadLEB_u32 (& funcTypeIndex, & i_bytes, i_end));

_       (Module_AddFunction (io_module, funcTypeIndex, NULL /* import info */));
    }

    _catch: return result;
}


M3Result  ParseSection_Import  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    M3ImportInfo import = { NULL, NULL }, clearImport = { NULL, NULL };

    u32 numImports;
_   (ReadLEB_u32 (& numImports, & i_bytes, i_end));                                 m3log (parse, "** Import [%d]", numImports);

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

                io_module->numImports++;
            }
            break;

            case d_externalKind_table:
//                  result = ParseType_Table (& i_bytes, i_end);
                break;

            case d_externalKind_memory:
            {
_               (ParseType_Memory (& io_module->memoryInfo, & i_bytes, i_end));
                io_module->memoryImported = true;
            }
            break;

            case d_externalKind_global:
            {
                i8 waType;
                u8 type, isMutable;

_               (ReadLEB_i7 (& waType, & i_bytes, i_end));
_               (NormalizeType (& type, waType));
_               (ReadLEB_u7 (& isMutable, & i_bytes, i_end));                     m3log (parse, "     global: %s mutable=%d", c_waTypes [type], (u32) isMutable);

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

    _catch:

    FreeImportInfo (& import);

    return result;
}


M3Result  ParseSection_Export  (IM3Module io_module, bytes_t i_bytes, cbytes_t  i_end)
{
    M3Result result = m3Err_none;

    u32 numExports;
_   (ReadLEB_u32 (& numExports, & i_bytes, i_end));                                 m3log (parse, "** Export [%d]", numExports);

    for (u32 i = 0; i < numExports; ++i)
    {
        const char * utf8;
        u8 exportKind;
        u32 index;

_       (Read_utf8 (& utf8, & i_bytes, i_end));
_       (Read_u8 (& exportKind, & i_bytes, i_end));
_       (ReadLEB_u32 (& index, & i_bytes, i_end));                                  m3log (parse, "    index: %3d; kind: %d; export: '%s'; ", index, (u32) exportKind, utf8);

        if (exportKind == d_externalKind_function)
        {
            _throwif(m3Err_wasmMalformed, index >= io_module->numFunctions);
            u16 numNames = io_module->functions [index].numNames;
            if (numNames < d_m3MaxDuplicateFunctionImpl)
            {
                io_module->functions [index].numNames++;
                io_module->functions [index].names[numNames] = utf8;
                utf8 = NULL; // ownership transferred to M3Function
            }
        }

        m3Free (utf8);
    }

    _catch: return result;
}


M3Result  ParseSection_Start  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 startFuncIndex;
_   (ReadLEB_u32 (& startFuncIndex, & i_bytes, i_end));                               m3log (parse, "** Start Function: %d", startFuncIndex);

    if (startFuncIndex < io_module->numFunctions)
    {
        io_module->startFunction = startFuncIndex;
    }
    else result = "start function index out of bounds";

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
    compilation = (M3Compilation){ NULL, io_module, * io_bytes, i_end };

    result = Compile_BlockStatements (& compilation);

    * io_bytes = compilation.wasm;

    return result;
}


M3Result  ParseSection_Element  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numSegments;
    result = ReadLEB_u32 (& numSegments, & i_bytes, i_end);                         m3log (parse, "** Element [%d]", numSegments);

    _throwif ("error parsing Element section", result);

    io_module->elementSection = i_bytes;
    io_module->elementSectionEnd = i_end;
    io_module->numElementSegments = numSegments;

    _catch: return result;
}


M3Result  ParseSection_Code  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result;

    u32 numFunctions;
_   (ReadLEB_u32 (& numFunctions, & i_bytes, i_end));                               m3log (parse, "** Code [%d]", numFunctions);

    if (numFunctions != io_module->numFunctions - io_module->numImports)
    {
        _throw ("mismatched function count in code section");
    }

    for (u32 f = 0; f < numFunctions; ++f)
    {
        u32 size;
_       (ReadLEB_u32 (& size, & i_bytes, i_end));

        if (size)
        {
            const u8 * ptr = i_bytes;
            i_bytes += size;

            if (i_bytes <= i_end)
            {
                const u8 * start = ptr;

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

                IM3Function func = Module_GetFunction (io_module, f + io_module->numImports);

                func->module = io_module;
                func->wasm = start;
                func->wasmEnd = i_bytes;
                //func->ownsWasmCode = io_module->hasWasmCodeCopy;
                func->numLocals = numLocals;
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

_   (m3Alloc (& io_module->dataSegments, M3DataSegment, numDataSegments));

    io_module->numDataSegments = numDataSegments;

    for (u32 i = 0; i < numDataSegments; ++i)
    {
        M3DataSegment * segment = & io_module->dataSegments [i];

_       (ReadLEB_u32 (& segment->memoryRegion, & i_bytes, i_end));

        segment->initExpr = i_bytes;
_       (Parse_InitExpr (io_module, & i_bytes, i_end));
        segment->initExprSize = (u32) (i_bytes - segment->initExpr);

        _throwif (m3Err_wasmMissingInitExpr, segment->initExprSize <= 1);

_       (ReadLEB_u32 (& segment->size, & i_bytes, i_end));
        segment->data = i_bytes;                                                    m3log (parse, "    segment [%u]  memory: %u;  expr-size: %d;  size: %d",
                                                                                       i, segment->memoryRegion, segment->initExprSize, segment->size);
        i_bytes += segment->size;
    }

    _catch:

    return result;
}


M3Result  ParseSection_Memory  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    // TODO: MVP; assert no memory imported

    u32 numMemories;
_   (ReadLEB_u32 (& numMemories, & i_bytes, i_end));                             m3log (parse, "** Memory [%d]", numMemories);

    _throwif (m3Err_tooManyMemorySections, numMemories != 1);

    ParseType_Memory (& io_module->memoryInfo, & i_bytes, i_end);

    _catch: return result;
}


M3Result  ParseSection_Global  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numGlobals;
_   (ReadLEB_u32 (& numGlobals, & i_bytes, i_end));                                 m3log (parse, "** Global [%d]", numGlobals);

    for (u32 i = 0; i < numGlobals; ++i)
    {
        i8 waType;
        u8 type, isMutable;

_       (ReadLEB_i7 (& waType, & i_bytes, i_end));
_       (NormalizeType (& type, waType));
_       (ReadLEB_u7 (& isMutable, & i_bytes, i_end));                                 m3log (parse, "    global: [%d] %s mutable: %d", i, c_waTypes [type],   (u32) isMutable);

        IM3Global global;
_       (Module_AddGlobal (io_module, & global, type, isMutable, false /* isImport */));

        global->initExpr = i_bytes;
_       (Parse_InitExpr (io_module, & i_bytes, i_end));
        global->initExprSize = (u32) (i_bytes - global->initExpr);

        _throwif (m3Err_wasmMissingInitExpr, global->initExprSize <= 1);
    }

    _catch: return result;
}


M3Result  ParseSection_Custom  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result;

    cstr_t name;
_   (Read_utf8 (& name, & i_bytes, i_end));
                                                                                    m3log (parse, "** Custom: '%s'", name);
    if (strcmp (name, "name") != 0)
        i_bytes = i_end;

    m3Free (name);

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

            for (u32 i = 0; i < numNames; ++i)
            {
                u32 index;
_               (ReadLEB_u32 (& index, & i_bytes, i_end));
_               (Read_utf8 (& name, & i_bytes, i_end));

                if (index < io_module->numFunctions)
                {
                    if (io_module->functions [index].numNames == 0)
                    {
                        io_module->functions [index].numNames = 1;
                        io_module->functions [index].names[0] = name;        m3log (parse, "    naming function%5d:  %s", index, name);
                        name = NULL; // transfer ownership
                    }
//                          else m3log (parse, "prenamed: %s", io_module->functions [index].name);
                }

                m3Free (name);
            }
        }

        i_bytes = start + payloadLength;
    }

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
        NULL,                   // 4: TODO Table
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
    M3Result result;

    IM3Module module;
_try {
_   (m3Alloc (& module, M3Module, 1));

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
                                                                                    m3log (parse,  "found magic + version");
    u8 previousSection = 0;

    while (pos < end)
    {
        u8 section;
_       (ReadLEB_u7 (& section, & pos, end));

        if (section > previousSection or                    // from the spec: sections must appear in order
            section == 0 or                                 // custom section
            (section == 12 and previousSection == 9) or     // if present, DataCount goes after Element
            (section == 10 and previousSection == 12))      // and before Code
        {
            u32 sectionLength;
_           (ReadLEB_u32 (& sectionLength, & pos, end));
            _throwif(m3Err_wasmMalformed, pos + sectionLength > end);
_           (ParseModuleSection (module, section, pos, sectionLength));

            pos += sectionLength;

            if (section)
                previousSection = section;
        }
        else _throw (m3Err_misorderedWasmSection);
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
