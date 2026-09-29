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


IM3Module m3_NewModule (IM3Environment i_environment)
{
    IM3Module module = m3_AllocStruct(M3Module);

    if (module) {
        module->name          = ".unnamed";
        module->startFunction = -1;
        module->environment   = i_environment;

        module->wasmStart = NULL;
        module->wasmEnd   = NULL;
    }

    return module;
}


M3Result m3_InjectFunction (IM3Module            i_module,
                            int32_t*             io_functionIndex,
                            const char* const    i_signature,
                            const uint8_t* const i_wasmBytes,
                            bool                 i_doCompilation)
{
    M3Result result = m3Err_none;                                       d_m3Assert (io_functionIndex);

    IM3Function function = NULL;
    IM3FuncType ftype    = NULL;

    // Declared before the first throw below, which jumps past this point to
    // _catch and so must not skip an initialization.
    i32     index;
    bytes_t bytes = i_wasmBytes;
    bytes_t end   = i_wasmBytes + 5;
    size_t  numBytes;
    u32     size;

_   (SignatureToFuncType(&ftype, i_signature));

    index = *io_functionIndex;

_   (ReadLEB_u32(&size, &bytes, end));
    end = bytes + size;

    if (index >= 0) {
        // index is known non-negative here, so the counts compare as unsigned
        _throwif("function index out of bounds", (u32)index >= i_module->numFunctions);

        function = &i_module->functions[index];

        if (not AreFuncTypesEqual(ftype, function->funcType)) {
            _throw("function type mismatch");
        }
    } else {
        // add slot to function type table in the module
        u32 funcTypeIndex   = i_module->numFuncTypes++;
        i_module->funcTypes = m3_ReallocArray(IM3FuncType, i_module->funcTypes, i_module->numFuncTypes, funcTypeIndex);
        _throwifnull(i_module->funcTypes);

        // add functype object to the environment
        Environment_AddFuncType(i_module->environment, &ftype);
        i_module->funcTypes[funcTypeIndex] = ftype;

        ftype = NULL; // prevent freeing below

        index = (i32)i_module->numFunctions;
_       (Module_AddFunction(i_module, funcTypeIndex, NULL));
        function = Module_GetFunction(i_module, index);

        *io_functionIndex = index;
    }

    function->compiled = NULL;

    if (function->ownsWasmCode) {
        m3_Free(function->wasm);
    }

    numBytes       = end - i_wasmBytes;
    function->wasm = m3_CopyMem(i_wasmBytes, numBytes);
    _throwifnull(function->wasm);

    function->wasmEnd      = function->wasm + numBytes;
    function->ownsWasmCode = true;

    function->module = i_module;

    if (i_doCompilation and not i_module->runtime) {
        _throw("module must be loaded into runtime to compile function");
    }

_   (CompileFunction(function));

    _catch:
    m3_Free(ftype);

    return result;
}


IM3Function m3_GetFunctionByIndex (IM3Module i_module, uint32_t i_index)
{
    return Module_GetFunction(i_module, i_index);
}


M3Result m3_ShareModule (IM3Runtime i_target, IM3Module i_source, IM3Module* o_proxy)
{
#if d_m3HasThreads
    M3Result  result = m3Err_none;
    IM3Module proxy  = NULL;
    u32       count  = 0;

    _throwif(m3Err_unknownMemory, not i_target or not i_source or not o_proxy);

    // what there is to share: the memories the source exports, that are shared
    for (u32 i = 0; i < i_source->numExports; ++i) {
        const M3Export* e = &i_source->exports[i];

        if (e->kind == d_externalKind_memory and i_source->memories[e->index] and
            i_source->memories[e->index]->shared) {
            ++count;
        }
    }

    _throwif("module exports no shared memory", count == 0);

    proxy = m3_NewModule(i_target->environment);
    _throwifnull(proxy);

    proxy->exports = m3_AllocArray(M3Export, count);
    _throwifnull(proxy->exports);

    for (u32 i = 0; i < i_source->numExports; ++i) {
        const M3Export* e    = &i_source->exports[i];
        IM3Memory       from = (e->kind == d_externalKind_memory) ? i_source->memories[e->index] : NULL;

        if (not from or not from->shared) {
            continue;
        }

        M3MemoryInfo info;
        info.initPages  = from->initPages;
        info.maxPages   = from->maxPages;
        info.pageSize   = from->pageSize;
        info.hasMax     = true;
        info.isMemory64 = from->isMemory64;
        info.isShared   = true;

        IM3Memory view = NULL;
_       (Module_AddMemory(proxy, &view, &info, false));
_       (AttachSharedMemory(i_target, view, from));

        // Named as the source names it. The name is a copy: exports own theirs.
        M3Export* entry = &proxy->exports[proxy->numExports];
        entry->name     = (cstr_t)m3_CopyMem(e->name, (size_t)e->nameLength + 1);
        _throwifnull(entry->name);
        entry->nameLength = e->nameLength;
        entry->index      = proxy->numMemories - 1;
        entry->kind       = d_externalKind_memory;
        ++proxy->numExports;
    }

_   (m3_LoadModule(i_target, proxy));

    *o_proxy = proxy;
    return result;

    _catch:
    // once loaded the runtime owns it, which m3_LoadModule has taken on even when it failed
    if (proxy and not proxy->runtime) {
        m3_FreeModule(proxy);
    }
    return result;
#else
    (void)i_target;
    (void)i_source;
    (void)o_proxy;
    return "sharing memories needs a build with d_m3HasThreads";
#endif
}
