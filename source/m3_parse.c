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


// elem type + limits, shared by the table section and by table imports.
// The declared minimum is the table's initial size; table.grow can raise it up
// to the maximum, so an element segment reaching past the current size is out
// of bounds.
#if d_m3HasTypedRefs

// Reads a heap type: 'func', 'extern', or the index of a function type the
// module has already defined. Yields the heap-type bits of an m3type_t, which
// carry the canonical index rather than the module's own, so that structurally
// equal types compare equal across modules.
M3Result  ParseHeapType  (IM3Module i_module, m3type_t * o_heapBits, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    i64 heap;
_   (ReadLebSigned (& heap, 33, io_bytes, i_end));

    if (heap == -d_waType_funcref)
        * o_heapBits = d_m3Type_heapAbstract;
    else if (heap == -d_waType_externref)
        * o_heapBits = d_m3Type_refExtern | d_m3Type_heapAbstract;
    else if (heap >= 0)
    {
        _throwif (m3Err_wasmMalformed, not i_module or (u64) heap >= i_module->numFuncTypes);

        IM3FuncType ftype = i_module->funcTypes [heap];

        // null while the type section is still being read: a type may only name
        // one that precedes it, recursion belongs to a later proposal
        _throwif (m3Err_wasmMalformed, not ftype);

        * o_heapBits = ftype->canonicalIndex;
    }
    else _throw (m3Err_invalidTypeId);

    _catch: return result;
}

#endif // d_m3HasTypedRefs


// Reads a value type, including the reference types the function references
// proposal spells out in full: (ref ht) and (ref null ht).
M3Result  ParseValueType  (IM3Module i_module, m3type_t * o_type, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    _throwif (m3Err_wasmUnderrun, * io_bytes >= i_end);

#if d_m3HasTypedRefs
    if (** io_bytes == d_waEncode_ref or ** io_bytes == d_waEncode_refNull)
    {
        u8 lead = * (* io_bytes)++;

        m3type_t heapBits = d_m3Type_heapAbstract;
_       (ParseHeapType (i_module, & heapBits, io_bytes, i_end));

        * o_type = d_m3Type_ref | heapBits | ((lead == d_waEncode_ref) ? d_m3Type_refNonNull : 0);

        return result;
    }
#endif

    i8 wasmType;
    u8 plainType;
_   (ReadLEB_i7 (& wasmType, io_bytes, i_end));
_   (NormalizeType (& plainType, wasmType));

    * o_type = plainType;

    _catch: return result;
}


// Reads an elem type and limits without registering anything: an import
// descriptor holds on to the fields and only adds the table once it has a name.
static
M3Result  ReadType_TableType  (IM3Module i_module, M3TableInfo * o_info, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    m3type_t elemType = c_m3Type_none;
    u8 flag = 0;
    u64 initSize = 0, maxSize = 0;

    // the width of the limits, and of every index into the table
    u32 addrBits = 32;

_   (ParseValueType (i_module, & elemType, io_bytes, i_end));
#if d_m3HasRefTypes
    _throwif (m3Err_wasmMalformed, not IsRefType (BaseTypeOf(elemType)));
#else
    _throwif (m3Err_wasmMalformed, elemType != c_m3Type_funcref);
#endif

_   (ReadLEB_u7 (& flag, io_bytes, i_end));

    // bit 0: has max, bit 2: table64 - the same flag memory64 gives a memory
#if d_m3HasMemory64
    _throwif (m3Err_wasmMalformed, flag & ~0x05u);
#else
    _throwif (m3Err_wasmMalformed, flag & ~0x01u);
#endif

    if (flag & 0x04u)
        addrBits = 64;

_   (ReadLebUnsigned (& initSize, addrBits, io_bytes, i_end));
    _throwif ("table overflow", initSize > d_m3MaxSaneTableSize);

    if (flag & 0x01u)
    {
_       (ReadLebUnsigned (& maxSize, addrBits, io_bytes, i_end));
        _throwif (m3Err_wasmMalformed, maxSize < initSize);
        _throwif ("table overflow", maxSize > d_m3MaxSaneTableSize);
    }

    _catch:

    o_info->elemType  = elemType;
    o_info->initSize  = (u32) initSize;
    o_info->maxSize   = (u32) maxSize;
    o_info->hasMax    = (flag & 0x01u) != 0;
    o_info->isTable64 = (flag & 0x04u) != 0;

    return result;
}


static
M3Result  ParseType_TableType  (IM3Module io_module, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    M3TableInfo info;

_   (ReadType_TableType (io_module, & info, io_bytes, i_end));
_   (Module_AddTable (io_module, NULL, & info, false /* isImport */));

    _catch: return result;
}



M3Result  Parse_InitExprTyped  (M3Module * io_module, bytes_t * io_bytes, cbytes_t i_end, m3type_t * o_type);

M3Result  ParseType_Table  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numTables;
_   (ReadLEB_u32 (& numTables, & i_bytes, i_end));                       m3log (parse, "** Table [%d]", numTables);

    _throwif ("too many tables", (u64) numTables + io_module->numTables > d_m3MaxSaneTableCount);

    for (u32 i = 0; i < numTables; ++i)
    {
#if d_m3HasTypedRefs
        // function-references: 0x40 0x00 introduces a table with an explicit
        // initializer. A reference type never starts with 0x40, so the two
        // forms are told apart by the first byte; the second is reserved.
        bool hasInitExpr = (i_bytes < i_end) and (* i_bytes == 0x40);
        u32 tableIndex = io_module->numTables;

        if (hasInitExpr)
        {
            u8 reserved;
            ++i_bytes;
_           (Read_u8 (& reserved, & i_bytes, i_end));
            _throwif (m3Err_wasmMalformed, reserved != 0x00);
        }

        _       (ParseType_TableType (io_module, & i_bytes, i_end));

        if (hasInitExpr)
        {
            IM3Table table = io_module->tables [tableIndex];

            m3type_t initType;

            table->initExpr = i_bytes;
_           (Parse_InitExprTyped (io_module, & i_bytes, i_end, & initType));
            table->initExprSize = (u32) (i_bytes - table->initExpr);
            _throwif (m3Err_wasmMissingInitExpr, table->initExprSize <= 1);

            _throwif (m3Err_typeMismatch, not IsSubTypeOf (initType, table->type));
        }
        else
        {
            // no initializer means null, which only a nullable type can hold
            _throwif ("table of non-nullable type requires an initializer",
                      not IsNullableRef (io_module->tables [tableIndex]->type));
        }
#else
_       (ParseType_TableType (io_module, & i_bytes, i_end));
#endif
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch: return result;
}


M3Result  ParseType_Memory  (M3MemoryInfo * o_memory, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u8 flag;

    // The custom page sizes proposal admits only 1 and 65536, but nothing in
    // the engine cares which power of two it is, so any of them is accepted.
    // Declared up here so the throws below don't jump over its initialization.
    u32 logPageSize = 16;

    // The width of the limits, and of everything that addresses the memory:
    // 32 unless bit 2 says memory64.
    u32 addrBits = 32;

_   (ReadLEB_u7 (& flag, io_bytes, i_end));

    // bit 0: has max, bit 2: memory64, bit 3: custom page size. bit 1 (shared)
    // belongs to a proposal we don't implement.
#if d_m3HasMemory64
    _throwif (m3Err_wasmMalformed, flag & ~0x0Du);
#else
    _throwif (m3Err_wasmMalformed, flag & ~0x09u);
#endif

    o_memory->isMemory64 = (flag & (1u << 2)) != 0;
    if (o_memory->isMemory64)
        addrBits = 64;

_   (ReadLebUnsigned (& o_memory->initPages, addrBits, io_bytes, i_end));

    o_memory->maxPages = 0;
    o_memory->hasMax = (flag & (1u << 0)) != 0;
    if (o_memory->hasMax)
    {
_       (ReadLebUnsigned (& o_memory->maxPages, addrBits, io_bytes, i_end));

        // Spec: memory limits validation - max must not be less than init
        _throwif (m3Err_wasmMalformed, o_memory->maxPages < o_memory->initPages);
    }

    o_memory->pageSize = 0;
    if (flag & (1u << 3)) {
_       (ReadLEB_u32 (& logPageSize, io_bytes, i_end));

        _throwif ("invalid custom page size", logPageSize > 16);

        o_memory->pageSize = 1u << logPageSize;
    }

    // Spec: memory limits must be valid within range 2^|addrtype|/pagesize.
    // That is 65536 pages at the default page size, 2^48 for a 64-bit memory,
    // and a whole address space of them when a page is a single byte.
    {
        u64 maxPagesAllowed = (logPageSize or addrBits < 64)
                                ? (UINT64_C(1) << (addrBits - logPageSize))
                                : UINT64_MAX;

        _throwif (m3Err_wasmMalformed, o_memory->initPages > maxPagesAllowed);
        if (o_memory->hasMax)
            _throwif (m3Err_wasmMalformed, o_memory->maxPages > maxPagesAllowed);
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
#if M3_HAS_VLA
            m3type_t argTypes[numArgs+1]; // make ubsan happy
#else
            m3type_t argTypes [d_m3MaxSaneFunctionArgRetCount];
#endif
            for (u32 a = 0; a < numArgs; ++a)
_               (ParseValueType (io_module, & argTypes[a], & i_bytes, i_end));

            u32 numRets;
_           (ReadLEB_u32 (& numRets, & i_bytes, i_end));
            _throwif (m3Err_tooManyArgsRets, (u64)(numRets) + numArgs > d_m3MaxSaneFunctionArgRetCount);

_           (AllocFuncType (& ftype, numRets + numArgs));
            ftype->numArgs = numArgs;
            ftype->numRets = numRets;

            for (u32 r = 0; r < numRets; ++r)
_               (ParseValueType (io_module, & ftype->types[r], & i_bytes, i_end));
            memcpy (ftype->types + numRets, argTypes, numArgs * sizeof (m3type_t));                                 m3log (parse, "    type %2d: %s", i, SPrintFuncTypeSignature (ftype));

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


// One externtype, read but not yet registered. The compact encoding that shares
// a type across a run needs the two halves apart: the type sits ahead of the
// item names, so it is read once and then registered for each of them.
typedef struct M3ImportDesc
{
    u8              kind;
    u32             typeIndex;                  // function
    m3type_t        type;                       // global type
    u8              isMutable;                  // global
    M3TableInfo     table;
    M3MemoryInfo    memory;
}
M3ImportDesc;


#if d_m3HasExceptionHandling

// A tag_type: a reserved attribute byte that must be 0 (the exception
// attribute) and the index of the function type giving the payload. The
// proposal reserves the byte so later kinds of tag can share the encoding.
static
M3Result  ReadTagType  (IM3Module i_module, u32 * o_typeIndex, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u8 attribute;
_   (Read_u8 (& attribute, io_bytes, i_end));
    _throwif (m3Err_wasmMalformed, attribute != 0);

_   (ReadLEB_u32 (o_typeIndex, io_bytes, i_end));
    _throwif (m3Err_unknownType, not i_module or * o_typeIndex >= i_module->numFuncTypes);

    // an exception tag's type describes its payload, so it yields nothing
    _throwif (m3Err_wasmMalformed, GetFuncTypeNumResults (i_module->funcTypes [* o_typeIndex]) != 0);

    _catch: return result;
}

#endif // d_m3HasExceptionHandling


// Reads and validates one externtype. Mutates nothing in the module, so an
// externtype is checked even when the compact run that shares it is empty.
static
M3Result  ReadImportDesc  (IM3Module i_module, M3ImportDesc * o_desc, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

_   (Read_u8 (& o_desc->kind, io_bytes, i_end));

    switch (o_desc->kind)
    {
        case d_externalKind_function:
_           (ReadLEB_u32 (& o_desc->typeIndex, io_bytes, i_end));
            break;

        case d_externalKind_table:
_           (ReadType_TableType (i_module, & o_desc->table, io_bytes, i_end));
            break;

        case d_externalKind_memory:
_           (ParseType_Memory (& o_desc->memory, io_bytes, i_end));
            break;

        case d_externalKind_global:
_           (ParseValueType (i_module, & o_desc->type, io_bytes, i_end));
_           (ReadLEB_u7 (& o_desc->isMutable, io_bytes, i_end));                    m3log (parse, "     global: %s mutable=%d", c_waTypes [BaseTypeOf(o_desc->type)], (u32) o_desc->isMutable);
            _throwif (m3Err_wasmMalformed, o_desc->isMutable > 1);
            break;

#if d_m3HasExceptionHandling
        case d_externalKind_tag:
_           (ReadTagType (i_module, & o_desc->typeIndex, io_bytes, i_end));
            break;
#endif

        default:
            _throw (m3Err_wasmMalformed);
    }

    _catch: return result;
}


// Registers the import that i_desc describes. Takes ownership of *io_import
// wherever the module keeps the strings, clearing the struct so the caller's
// FreeImportInfo() doesn't free what was handed over.
static
M3Result  ApplyImportDesc  (IM3Module io_module, const M3ImportDesc * i_desc, M3ImportInfo * io_import)
{
    M3Result result = m3Err_none;

    M3ImportInfo clearImport = { NULL, NULL };                                      m3log (parse, "    kind: %d '%s.%s' ",
                                                                                            (u32) i_desc->kind, io_import->moduleUtf8, io_import->fieldUtf8);
    switch (i_desc->kind)
    {
        case d_externalKind_function:
        {
_           (Module_AddFunction (io_module, i_desc->typeIndex, io_import))
            * io_import = clearImport;

            io_module->numFuncImports++;
        }
        break;

        case d_externalKind_table:
        {
            IM3Table table;
_           (Module_AddTable (io_module, & table, & i_desc->table, true /* isImport */));
            table->import = * io_import;
            * io_import = clearImport;
        }
        break;

        case d_externalKind_memory:
        {
            IM3Memory memory;

#if d_m3HasMultiMemory
            _throwif (m3Err_tooManyMemorySections, io_module->numMemories >= d_m3MaxSaneMemoriesCount);
#else
            // MVP: one memory per module, imported or declared
            _throwif (m3Err_tooManyMemorySections, io_module->numMemories >= 1);
#endif

_           (Module_AddMemory (io_module, & memory, & i_desc->memory, true /* isImport */));
            memory->import = * io_import;
            * io_import = clearImport;
        }
        break;

        case d_externalKind_global:
        {
            IM3Global global;
_           (Module_AddGlobal (io_module, & global, i_desc->type, i_desc->isMutable, true /* isImport */));
            global->import = * io_import;
            * io_import = clearImport;
        }
        break;

#if d_m3HasExceptionHandling
        case d_externalKind_tag:
        {
            IM3Tag tag;
_           (Module_AddTag (io_module, & tag, io_module->funcTypes [i_desc->typeIndex], true /* isImport */));
            tag->import = * io_import;
            * io_import = clearImport;
        }
        break;
#endif

        default:
            _throw (m3Err_wasmMalformed);
    }

    _catch: return result;
}


#if d_m3HasCompactImports

// The compact encodings only apply when the item name is empty. Read_utf8()
// hands back a C string, so a name that merely begins with U+0000 would look
// empty; the length prefix is what has to be tested.
static
bool  IsEmptyName  (bytes_t i_bytes, cbytes_t i_end)
{
    u32 length;
    return (not ReadLEB_u32 (& length, & i_bytes, i_end)) and length == 0;
}

#endif // d_m3HasCompactImports


#if d_m3HasExceptionHandling

M3Result  ParseSection_Tag  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numTags;
_   (ReadLEB_u32 (& numTags, & i_bytes, i_end));                                    m3log (parse, "** Tag [%d]", numTags);

    _throwif ("too many tags", numTags > d_m3MaxSaneTagsCount);

    for (u32 i = 0; i < numTags; ++i)
    {
        u32 typeIndex;
_       (ReadTagType (io_module, & typeIndex, & i_bytes, i_end));                   m3log (parse, "    tag: [%d] type: %d", i, typeIndex);

_       (Module_AddTag (io_module, NULL, io_module->funcTypes [typeIndex], false));
    }

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch: return result;
}

#endif // d_m3HasExceptionHandling


M3Result  ParseSection_Import  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    M3ImportInfo import = { NULL, NULL };

    // the limit is on the imports the section declares, which a compact run
    // multiplies well past the entry count. Declared up here so the throws
    // below don't jump over its initialization.
    u32 numImports = 0;

    // A count of entries, not of imports: under the compact encodings a single
    // entry stands for a whole run of them.
    u32 numEntries;
_   (ReadLEB_u32 (& numEntries, & i_bytes, i_end));                                 m3log (parse, "** Import [%d]", numEntries);

    _throwif("too many imports", numEntries > d_m3MaxSaneImportsCount);

    // Most imports are functions, so we won't waste much space anyway (if any)
_   (Module_PreallocFunctions(io_module, io_module->numFunctions + numEntries));

    for (u32 i = 0; i < numEntries; ++i)
    {
        M3ImportDesc desc;

_       (Read_utf8 (& import.moduleUtf8, & i_bytes, i_end));

#if d_m3HasCompactImports
        bytes_t fieldStart = i_bytes;
#endif
_       (Read_utf8 (& import.fieldUtf8, & i_bytes, i_end));

#if d_m3HasCompactImports
        u8 compact = (i_bytes < i_end) ? * i_bytes : 0;

        // An empty item name where an externtype should start marks a compact
        // run sharing this module name. Neither marker is a valid externtype,
        // so nothing that used to parse changes meaning.
        if ((compact == d_compactImports_perItemType or compact == d_compactImports_sharedType) and
            IsEmptyName (fieldStart, i_end))
        {
            ++i_bytes;

            m3_Free (import.fieldUtf8);         // it only marked the compact form
            import.fieldUtf8 = NULL;

            // the shared form puts its one externtype ahead of the item names,
            // so reading it here is also what validates it
            if (compact == d_compactImports_sharedType)
_               (ReadImportDesc (io_module, & desc, & i_bytes, i_end));

            u32 numItems;
_           (ReadLEB_u32 (& numItems, & i_bytes, i_end));                           m3log (parse, "    compact: %d import(s) of '%s'", numItems, import.moduleUtf8);

            _throwif("too many imports", (u64) numImports + numItems > d_m3MaxSaneImportsCount);
            numImports += numItems;

_           (Module_PreallocFunctions(io_module, io_module->numFunctions + numItems));

            for (u32 j = 0; j < numItems; ++j)
            {
                M3ImportInfo item = { NULL, NULL };

                item.moduleUtf8 = (cstr_t) m3_CopyMem (import.moduleUtf8, strlen (import.moduleUtf8) + 1);

                if (item.moduleUtf8)
                {
                    result = Read_utf8 (& item.fieldUtf8, & i_bytes, i_end);

                    // the shared form reuses the externtype read above
                    if (not result and compact == d_compactImports_perItemType)
                        result = ReadImportDesc (io_module, & desc, & i_bytes, i_end);

                    if (not result)
                        result = ApplyImportDesc (io_module, & desc, & item);
                }
                else result = m3Err_mallocFailed;

                FreeImportInfo (& item);
                _throwif (result, result);
            }

            FreeImportInfo (& import);
            continue;
        }
#endif // d_m3HasCompactImports

        _throwif("too many imports", numImports >= d_m3MaxSaneImportsCount);
        ++numImports;

_       (ReadImportDesc (io_module, & desc, & i_bytes, i_end));
_       (ApplyImportDesc (io_module, & desc, & import));

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
_           (Module_DeclareFunction (io_module, index));
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
            _throwif(m3Err_wasmMalformed, index >= io_module->numMemories);
            IM3Memory memory = io_module->memories [index];
            m3_Free (memory->exportName);
            memory->exportName = utf8;
            utf8 = NULL; // ownership transferred to M3Memory
        }
        else if (exportKind == d_externalKind_table)
        {
            _throwif(m3Err_wasmMalformed, index >= io_module->numTables);
            IM3Table table = io_module->tables [index];
            m3_Free (table->exportName);
            table->exportName = utf8;
            utf8 = NULL; // ownership transferred to M3Table
        }
#if d_m3HasExceptionHandling
        else if (exportKind == d_externalKind_tag)
        {
            _throwif(m3Err_wasmMalformed, index >= io_module->numTags);
            IM3Tag tag = &(io_module->tags [index]);
            m3_Free (tag->name);
            tag->name = utf8;
            utf8 = NULL; // ownership transferred to M3Tag
        }
#endif

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


// o_type, when asked for, reports what the expression leaves on the stack, so
// the caller can check it against the type the expression is initializing.
M3Result  Parse_InitExprTyped  (M3Module * io_module, bytes_t * io_bytes, cbytes_t i_end, m3type_t * o_type)
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

    if (o_type)
    {
        * o_type = compilation.stackIndex ? compilation.typeStack [compilation.stackIndex - 1]
                                          : (m3type_t) c_m3Type_none;
    }

    return result;
}


M3Result  Parse_InitExpr  (M3Module * io_module, bytes_t * io_bytes, cbytes_t i_end)
{
    return Parse_InitExprTyped (io_module, io_bytes, i_end, NULL);
}


M3Result  ParseSection_Element  (IM3Module io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    u32 numSegments;
    bytes_t pos;
_   (ReadLEB_u32 (& numSegments, & i_bytes, i_end));                         m3log (parse, "** Element [%d]", numSegments);

    _throwif ("too many element segments", numSegments > d_m3MaxSaneElementSegments);

    io_module->elementSegments = m3_AllocArray (M3ElementSegment, numSegments);
    _throwifnull (io_module->elementSegments);
    io_module->numElementSegments = numSegments;
    io_module->elementSectionEnd = i_end;

    // Records where each segment's parts live; the elements are resolved later,
    // in InitTableAndElements.
    pos = i_bytes;
    for (u32 i = 0; i < numSegments; ++i)
    {
        M3ElementSegment * segment = & io_module->elementSegments [i];

        u32 flags;
_       (ReadLEB_u32 (& flags, & pos, i_end));
#if d_m3HasRefTypes
        _throwif (m3Err_wasmMalformed, flags > 7);
#else
        _throwif (m3Err_wasmMalformed, flags != c_m3Elem_active);
#endif

        segment->mode   = flags & 0x3;
        segment->isExpr = (flags & 0x4) != 0;
        segment->type   = c_m3Type_funcref;

        if (segment->mode == c_m3Elem_activeIdx)
_           (ReadLEB_u32 (& segment->tableIndex, & pos, i_end));

        bool isActive = (segment->mode == c_m3Elem_active or segment->mode == c_m3Elem_activeIdx);

        if (isActive)
        {
            _throwif (m3Err_wasmMalformed, segment->tableIndex >= io_module->numTables);

            segment->initExpr = pos;
_           (Parse_InitExpr (io_module, & pos, i_end));
            segment->initExprSize = (u32) (pos - segment->initExpr);
            _throwif (m3Err_wasmMissingInitExpr, segment->initExprSize <= 1);
        }

        // Only the table-0 active form leaves the element type implicit
        if (segment->mode != c_m3Elem_active)
        {
            if (segment->isExpr)
            {
_               (ParseValueType (io_module, & segment->type, & pos, i_end));
                _throwif (m3Err_wasmMalformed, not IsRefType (BaseTypeOf(segment->type)));
            }
            else
            {
                u8 elemKind;
_               (Read_u8 (& elemKind, & pos, i_end));
                _throwif (m3Err_wasmMalformed, elemKind != 0x00);   // funcref
            }
        }

        if (isActive)
            _throwif (m3Err_typeMismatch, io_module->tables [segment->tableIndex]->type != segment->type);

_       (ReadLEB_u32 (& segment->numElements, & pos, i_end));
        _throwif ("table overflow", segment->numElements > d_m3MaxSaneTableSize);

        segment->elements = pos;

        for (u32 e = 0; e < segment->numElements; ++e)
        {
            if (segment->isExpr)
_               (Parse_InitExpr (io_module, & pos, i_end))
            else
            {
                u32 funcIndex;
_               (ReadLEB_u32 (& funcIndex, & pos, i_end));
                _throwif ("function index out of range", funcIndex >= io_module->numFunctions);
_               (Module_DeclareFunction (io_module, funcIndex));
            }
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
            i_bytes += size;

            if (i_bytes <= i_end)
            {
                /*
                const u8 * ptr = i_bytes - size;

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

        // 0: active in memory 0, 1: passive, 2: active with an explicit memory index
        u32 flags;
_       (ReadLEB_u32 (& flags, & i_bytes, i_end));
        _throwif (m3Err_wasmMalformed, flags > 2);

        segment->isPassive = (flags == 1);

        if (flags == 2)
        {
_           (ReadLEB_u32 (& segment->memoryRegion, & i_bytes, i_end));
        }

        if (not segment->isPassive)
        {
            // The segment names the memory it initializes; only multi-memory
            // lets that be anything other than 0.
#if ! d_m3HasMultiMemory
            _throwif (m3Err_wasmMalformed, segment->memoryRegion != 0);
#endif
            _throwif (m3Err_wasmMalformed, segment->memoryRegion >= io_module->numMemories);

            segment->initExpr = i_bytes;
_           (Parse_InitExpr (io_module, & i_bytes, i_end));
            segment->initExprSize = (u32) (i_bytes - segment->initExpr);

            _throwif (m3Err_wasmMissingInitExpr, segment->initExprSize <= 1);
        }

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


M3Result  ParseSection_DataCount  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

_   (ReadLEB_u32 (& io_module->dataCount, & i_bytes, i_end));                    m3log (parse, "** DataCount [%d]", io_module->dataCount);

    io_module->hasDataCount = true;

    _throwif (m3Err_wasmMalformed, i_bytes != i_end);      // section size mismatch

    _catch: return result;
}


M3Result  ParseSection_Memory  (M3Module * io_module, bytes_t i_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    // TODO: MVP; assert no memory imported

    u32 numMemories;
_   (ReadLEB_u32 (& numMemories, & i_bytes, i_end));                             m3log (parse, "** Memory [%d]", numMemories);

#if d_m3HasMultiMemory
    _throwif (m3Err_tooManyMemorySections,
              (u64) numMemories + io_module->numMemories > d_m3MaxSaneMemoriesCount);
#else
    // MVP: at most one memory, counting any that was already imported
    _throwif (m3Err_tooManyMemorySections, numMemories > 1);
    _throwif (m3Err_tooManyMemorySections, (u64) numMemories + io_module->numMemories > 1);
#endif

    for (u32 i = 0; i < numMemories; ++i)
    {
        M3MemoryInfo info;
_       (ParseType_Memory (& info, & i_bytes, i_end));
_       (Module_AddMemory (io_module, NULL, & info, false /* isImport */));
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
        m3type_t type = c_m3Type_none;
        u8 isMutable;

_       (ParseValueType (io_module, & type, & i_bytes, i_end));
_       (ReadLEB_u7 (& isMutable, & i_bytes, i_end));                                 m3log (parse, "    global: [%d] %s mutable: %d", i, c_waTypes [BaseTypeOf(type)],   (u32) isMutable);
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
    M3Result result = m3Err_none;

    cstr_t name = NULL;
_   (Read_utf8 (& name, & i_bytes, i_end));
                                                                                    m3log (parse, "** Custom: '%s'", name);
    if (strcmp (name, "name") == 0) {
_       (ParseSection_Name(io_module, i_bytes, i_end));
    } else if (io_module->environment->customSectionHandler) {
_       (io_module->environment->customSectionHandler(io_module, name, i_bytes, i_end));
    }

    // the section name is freed on the error path too: a malformed "name"
    // section, or a handler that rejects the payload, still throws past here
    _catch:

    m3_Free (name);

    return result;
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
        ParseSection_DataCount, // 12
#if d_m3HasExceptionHandling
        ParseSection_Tag,       // 13
#endif
    };

    M3Parser parser = NULL;

    if (i_sectionType < M3_COUNT_OF (s_parsers))
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

#if d_m3HasExceptionHandling
    // the tag section sits between memory (5) and global (6)
    static const u8 sectionsOrder[] = { 1, 2, 3, 4, 5, 13, 6, 7, 8, 9, 12, 10, 11, 0 }; // 0 is a placeholder
#else
    static const u8 sectionsOrder[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 10, 11, 0 }; // 0 is a placeholder
#endif
    u8 expectedSection = 0;

    while (pos < end)
    {
        u8 section;
_       (ReadLEB_u7 (& section, & pos, end));

        if (section != 0) {
            // Ensure sections appear only once and in order
            while (sectionsOrder[expectedSection++] != section) {
                _throwif(m3Err_misorderedWasmSection, expectedSection >= M3_COUNT_OF (sectionsOrder) - 1);
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

    // Spec: the data count section must agree with the data section, which may be absent
    _throwif (m3Err_wasmMalformed, module->hasDataCount and module->dataCount != module->numDataSegments);

} _catch:

    if (result)
    {
        m3_FreeModule (module);
        module = NULL;
    }

    * o_module = module;

    return result;
}
