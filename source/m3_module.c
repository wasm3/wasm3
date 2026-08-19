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

        m3_Free (i_module->functions);
        //m3_Free (i_module->imports);
        m3_Free (i_module->funcTypes);
        m3_Free (i_module->dataSegments);

        for (u32 i = 0; i < i_module->numTables; ++i)
            m3_Free (i_module->tables[i].elements);
        m3_Free (i_module->tables);

        for (u32 i = 0; i < i_module->numElementSegments; ++i)
            m3_Free (i_module->elementSegments[i].resolved);
        m3_Free (i_module->elementSegments);
        m3_Free (i_module->declaredFuncs);

        for (u32 i = 0; i < i_module->numGlobals; ++i)
        {
            m3_Free (i_module->globals[i].name);
            FreeImportInfo(&(i_module->globals[i].import));
        }
        m3_Free (i_module->globals);
        m3_Free (i_module->memoryExportName);
        m3_Free (i_module->table0ExportName);

        FreeImportInfo(&i_module->memoryImport);

        m3_Free (i_module);
    }
}


// Supplies the value of an imported global. Unlike m3_SetGlobal this ignores
// mutability: the import's mutability governs what the wasm code may do with it,
// not whether the host may provide it in the first place.
M3Result  m3_LinkGlobal  (IM3Module            io_module,
                          const char * const   i_moduleName,
                          const char * const   i_globalName,
                          const IM3TaggedValue i_value)
{
    M3Result result = m3Err_globalLookupFailed;

    for (u32 i = 0; i < io_module->numGlobals; ++i)
    {
        IM3Global g = & io_module->globals [i];

        if (not (g->import.moduleUtf8 and g->import.fieldUtf8))
            continue;

        if (strcmp (g->import.moduleUtf8, i_moduleName) != 0 or
            strcmp (g->import.fieldUtf8, i_globalName) != 0)
            continue;

        if (g->type != i_value->type)
            return m3Err_globalTypeMismatch;

        switch (i_value->type) {
        case c_m3Type_i32: g->i32Value = i_value->value.i32; break;
        case c_m3Type_i64: g->i64Value = i_value->value.i64; break;
# if d_m3HasFloat
        case c_m3Type_f32: g->f32Value = i_value->value.f32; break;
        case c_m3Type_f64: g->f64Value = i_value->value.f64; break;
# endif
        default: return m3Err_invalidTypeId;
        }

        result = m3Err_none;
    }

    return result;
}


// The function count is final by the time anything can declare a reference:
// imports and the function section both precede the global, export and element
// sections, and those in turn precede the code section.
M3Result  Module_DeclareFunction  (IM3Module io_module, u32 i_index)
{
    M3Result result = m3Err_none;

    if (i_index >= io_module->numFunctions)
        return "function index out of range";

    if (not io_module->declaredFuncs)
    {
        io_module->declaredFuncs = m3_AllocArray (u8, (io_module->numFunctions + 7) / 8);
        _throwifnull (io_module->declaredFuncs);
    }

    io_module->declaredFuncs [i_index / 8] |= (u8) (1u << (i_index % 8));

    _catch: return result;
}


bool  Module_IsFunctionDeclared  (IM3Module i_module, u32 i_index)
{
    if (not i_module->declaredFuncs or i_index >= i_module->numFunctions)
        return false;

    return (i_module->declaredFuncs [i_index / 8] & (1u << (i_index % 8))) != 0;
}


M3Result  Module_AddTable  (IM3Module io_module, m3type_t i_type, u32 i_size, u32 i_maxSize)
{
_try {
    u32 index = io_module->numTables++;
    io_module->tables = m3_ReallocArray (M3Table, io_module->tables, io_module->numTables, index);
    _throwifnull (io_module->tables);
    M3Table * table = & io_module->tables [index];

    table->type = i_type;
    table->size = i_size;
    table->maxSize = i_maxSize;

} _catch:
    return result;
}


M3Result  Module_AddGlobal  (IM3Module io_module, IM3Global * o_global, m3type_t i_type, bool i_mutable, bool i_isImported)
{
_try {
    u32 index = io_module->numGlobals++;
    io_module->globals = m3_ReallocArray (M3Global, io_module->globals, io_module->numGlobals, index);
    _throwifnull (io_module->globals);
    M3Global * global = & io_module->globals [index];

    global->type = i_type;
    global->imported = i_isImported;
    global->isMutable = i_mutable;

    if (o_global)
        * o_global = global;

} _catch:
    return result;
}

M3Result  Module_PreallocFunctions  (IM3Module io_module, u32 i_totalFunctions)
{
_try {
    if (i_totalFunctions > io_module->allFunctions) {
        io_module->functions = m3_ReallocArray (M3Function, io_module->functions, i_totalFunctions, io_module->allFunctions);
        io_module->allFunctions = i_totalFunctions;
        _throwifnull (io_module->functions);
    }
} _catch:
    return result;
}

M3Result  Module_AddFunction  (IM3Module io_module, u32 i_typeIndex, IM3ImportInfo i_importInfo)
{
_try {

    u32 index = io_module->numFunctions++;
_   (Module_PreallocFunctions(io_module, io_module->numFunctions));

    _throwif ("type sig index out of bounds", i_typeIndex >= io_module->numFuncTypes);

    IM3FuncType ft = io_module->funcTypes [i_typeIndex];

    IM3Function func = Module_GetFunction (io_module, index);
    func->funcType = ft;
    func->module = io_module;       // an import has no body, but still belongs to this module

#   ifdef DEBUG
    func->index = index;
#   endif

    if (i_importInfo and func->numNames == 0)
    {
        func->import = * i_importInfo;
        func->names[0] = i_importInfo->fieldUtf8;
        func->numNames = 1;
    }

    m3log (module, "   added function: %3d; sig: %d", index, i_typeIndex);

} _catch:
    return result;
}

#ifdef DEBUG
void  Module_GenerateNames  (IM3Module i_module)
{
    for (u32 i = 0; i < i_module->numFunctions; ++i)
    {
        IM3Function func = & i_module->functions [i];

        if (func->numNames == 0)
        {
            char* buff = m3_AllocArray(char, 16);
            snprintf(buff, 16, "$func%d", i);
            func->names[0] = buff;
            func->numNames = 1;
        }
    }
    for (u32 i = 0; i < i_module->numGlobals; ++i)
    {
        IM3Global global = & i_module->globals [i];

        if (global->name == NULL)
        {
            char* buff = m3_AllocArray(char, 16);
            snprintf(buff, 16, "$global%d", i);
            global->name = buff;
        }
    }
}
#endif

IM3Function  Module_GetFunction  (IM3Module i_module, u32 i_functionIndex)
{
    IM3Function func = NULL;

    if (i_functionIndex < i_module->numFunctions)
    {
        func = & i_module->functions [i_functionIndex];
        //func->module = i_module;
    }

    return func;
}


const char*  m3_GetModuleName  (IM3Module i_module)
{
    if (!i_module || !i_module->name)
        return ".unnamed";

    return i_module->name;
}

void  m3_SetModuleName  (IM3Module i_module, const char* name)
{
    if (i_module) i_module->name = name;
}

IM3Runtime  m3_GetModuleRuntime  (IM3Module i_module)
{
    return i_module ? i_module->runtime : NULL;
}

