//
//  m3_env.c
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <float.h>
#include <ctype.h>

#include "m3_env.h"
#include "m3_deterministic.h"
#include "m3_compile.h"
#include "m3_host.h"
#include "m3_exception.h"
#include "m3_info.h"


IM3Environment m3_NewEnvironment ()
{
    IM3Environment env = m3_AllocStruct(M3Environment);

    if (env) {
        _try
        {
            // create FuncTypes for all simple block return ValueTypes.
            // v128 is skipped: it parses as a slot but has no operations.
            for (u8 t = c_m3Type_none; t < c_m3Type_count; t++) {
                if (t == c_m3Type_v128) {
                    continue;
                }

                IM3FuncType ftype;
_               (AllocFuncType(&ftype, 1));

                ftype->numArgs  = 0;
                ftype->numRets  = (t == c_m3Type_none) ? 0 : 1;
                ftype->types[0] = t;

                Environment_AddFuncType(env, &ftype);

                env->retFuncTypes[t] = ftype;
            }
        }

        _catch:
        if (result) {
            m3_FreeEnvironment(env);
            env = NULL;
        }
    }

    return env;
}


void Environment_Release (IM3Environment i_environment)
{
    IM3FuncType ftype = i_environment->funcTypes;

    while (ftype) {
        IM3FuncType next = ftype->next;
        m3_Free(ftype);
        ftype = next;
    }

    m3log(runtime, "freeing %d pages from environment", CountCodePages(i_environment->pagesReleased));
    FreeCodePages(&i_environment->pagesReleased);
}


void m3_FreeEnvironment (IM3Environment i_environment)
{
    if (i_environment) {
        Environment_Release(i_environment);
        m3_Free(i_environment);
    }
}


void m3_SetCustomSectionHandler (IM3Environment i_environment, M3SectionHandler i_handler)
{
    if (i_environment) {
        i_environment->customSectionHandler = i_handler;
    }
}


// returns the same io_funcType or replaces it with an equivalent that's already in the type linked list
M3Result Environment_AddFuncType (IM3Environment i_environment, IM3FuncType* io_funcType)
{
    IM3FuncType addType = *io_funcType;
    IM3FuncType newType = i_environment->funcTypes;

    while (newType) {
        if (not newType->inRecGroup and AreFuncTypesEqual(newType, addType)) {
            m3_Free(addType);
            break;
        }

        newType = newType->next;
    }

    if (newType == NULL) {
        // a type index has to fit in the heap type field of an m3type_t
        if (i_environment->numFuncTypes >= d_m3MaxSaneTypesCount) {
            m3_Free(addType);
            *io_funcType = NULL;
            return "too many distinct function types";
        }

        newType                  = addType;
        newType->canonicalIndex  = i_environment->numFuncTypes++;
        newType->next            = i_environment->funcTypes;
        i_environment->funcTypes = newType;
    }

    *io_funcType = newType;

    return m3Err_none;
}


// Adds a type without looking for an equal one first, and hands back the index
// it was given.
//
// Members of a recursive group do not get to share an entry with a type that
// merely looks like them: the group is their identity, so two groups spelled
// the same way are still two groups, and a type inside one is never the type
// outside it that matches field for field. Reserving the indices up front is
// also what lets the members refer to each other while they are being read.
M3Result Environment_ReserveFuncTypes (IM3Environment i_environment, u32 i_count, u16* o_firstIndex)
{
    if ((u64)i_environment->numFuncTypes + i_count > d_m3MaxSaneTypesCount) {
        return "too many distinct function types";
    }

    *o_firstIndex = i_environment->numFuncTypes;
    i_environment->numFuncTypes += (u16)i_count;

    return m3Err_none;
}


void Environment_AdoptFuncType (IM3Environment i_environment, IM3FuncType i_funcType, u16 i_canonicalIndex)
{
    i_funcType->inRecGroup     = true;
    i_funcType->canonicalIndex = i_canonicalIndex;
    i_funcType->next           = i_environment->funcTypes;
    i_environment->funcTypes   = i_funcType;
}


IM3CodePage RemoveCodePageOfCapacity (M3CodePage** io_list, u32 i_minimumLineCount)
{
    IM3CodePage prev = NULL;
    IM3CodePage page = *io_list;

    while (page) {
        if (NumFreeLines(page) >= i_minimumLineCount) {             d_m3Assert (page->info.usageCount == 0);
            IM3CodePage next = page->info.next;
            if (prev) {
                prev->info.next = next; // mid-list
            } else {
                *io_list = next;       // front of list
            }

            break;
        }

        prev = page;
        page = page->info.next;
    }

    return page;
}


IM3CodePage Environment_AcquireCodePage (IM3Environment i_environment, u32 i_minimumLineCount)
{
    return RemoveCodePageOfCapacity(&i_environment->pagesReleased, i_minimumLineCount);
}


void Environment_ReleaseCodePages (IM3Environment i_environment, IM3CodePage i_codePageList)
{
    IM3CodePage end = i_codePageList;

    while (end) {
        end->info.lineIndex = 0; // reset page
#if d_m3RecordBacktraces
        end->info.mapping->size = 0;
#endif // d_m3RecordBacktraces

        IM3CodePage next = end->info.next;
        if (not next) {
            break;
        }

        end = next;
    }

    if (end) {
        // push list to front
        end->info.next               = i_environment->pagesReleased;
        i_environment->pagesReleased = i_codePageList;
    }
}


IM3Runtime m3_NewRuntime (IM3Environment i_environment, u32 i_stackSizeInBytes, void* i_userdata)
{
    IM3Runtime runtime = m3_AllocStruct(M3Runtime);

    if (runtime) {
        m3_ResetErrorInfo(runtime);

        runtime->environment = i_environment;
        runtime->userdata    = i_userdata;

        Deterministic_InitRuntime(runtime);

        runtime->originStack = m3_Malloc("Wasm Stack", i_stackSizeInBytes + 4 * sizeof(m3slot_t)); // TODO: more precise stack checks

        if (runtime->originStack) {
            runtime->stack         = runtime->originStack;
            runtime->numStackSlots = i_stackSizeInBytes / sizeof(m3slot_t);          m3log (runtime, "new stack: %p, slots: %u", runtime->originStack, runtime->numStackSlots);
        } else {
            m3_Free(runtime);
        }
    }

    return runtime;
}

void m3_SetValidation (IM3Runtime i_runtime, bool i_enable)
{
#if d_m3EnableValidation
    if (i_runtime) {
        i_runtime->skipValidation = not i_enable;
    }
#else
    (void)i_runtime;
    (void)i_enable;              // nothing to skip: the validator was compiled out
#endif
}

void m3_SetGasLimit (IM3Runtime i_runtime, double i_gas)
{
#if d_m3HasGasMetering
    if (i_runtime) {
        // A budget bigger than the counter can hold is the same as no ceiling
        // worth speaking of, so it saturates rather than wrapping
        const double maxGas = (double)INT64_MAX / d_m3GasUnitsPerGas;

        i64 units;
        if (i_gas >= maxGas) {
            units = INT64_MAX;
        } else if (i_gas > 0) {
            units = (i64)(i_gas * d_m3GasUnitsPerGas);
        } else {
            units = 0;
        }

        i_runtime->gasLimit = i_runtime->gasRemaining = units;
    }
#else
    (void)i_runtime;
    (void)i_gas;                 // nothing to meter: the instrumentation was compiled out
#endif
}

double m3_GetGasLimit (IM3Runtime i_runtime)
{
#if d_m3HasGasMetering
    if (i_runtime) {
        return (double)i_runtime->gasLimit / d_m3GasUnitsPerGas;
    }
#else
    (void)i_runtime;
#endif
    return 0;
}

double m3_GetGasUsed (IM3Runtime i_runtime)
{
#if d_m3HasGasMetering
    if (i_runtime) {
        return (double)(i_runtime->gasLimit - i_runtime->gasRemaining) / d_m3GasUnitsPerGas;
    }
#else
    (void)i_runtime;
#endif
    return 0;
}


void* m3_GetUserData (IM3Runtime i_runtime)
{
    return i_runtime ? i_runtime->userdata : NULL;
}


void* ForEachModule (IM3Runtime i_runtime, ModuleVisitor i_visitor, void* i_info)
{
    void* r = NULL;

    IM3Module module = i_runtime->modules;

    while (module) {
        IM3Module next = module->next;
        if ((r = i_visitor(module, i_info))) {
            break;
        }

        module = next;
    }

    return r;
}


void* _FreeModule (IM3Module i_module, void* i_info)
{
    m3_FreeModule(i_module);
    return NULL;
}


// A module's memory and table index spaces hold borrowed pointers: linking
// repoints an imported slot at the memory or table the exporting module owns.
// m3_FreeModule tells a borrowed slot from an owned one by reading that owner,
// which only holds while the object is still allocated - and the free walk
// below visits modules in load order, with no ordering between an owner and
// the modules borrowing from it. So clear the borrowed slots first, in a pass
// that frees nothing; the walk then skips them on the NULL check it already
// does, instead of reading a memory some earlier module has freed.
void* _ReleaseBorrowedSlots (IM3Module i_module, void* i_info)
{
    for (u32 i = 0; i < i_module->numMemories; ++i) {
        IM3Memory memory = i_module->memories[i];

        if (memory and memory->owner != i_module) {
            i_module->memories[i] = NULL;
        }
    }

    for (u32 i = 0; i < i_module->numTables; ++i) {
        IM3Table table = i_module->tables[i];

        if (table and table->owner != i_module) {
            i_module->tables[i] = NULL;
        }
    }

    return NULL;
}


void Runtime_Release (IM3Runtime i_runtime)
{
    ForEachModule(i_runtime, _ReleaseBorrowedSlots, NULL);
    ForEachModule(i_runtime, _FreeModule, NULL);                    d_m3Assert (i_runtime->numActiveCodePages == 0);

    Environment_ReleaseCodePages(i_runtime->environment, i_runtime->pagesOpen);
    Environment_ReleaseCodePages(i_runtime->environment, i_runtime->pagesFull);

    m3_Free(i_runtime->originStack);

#if d_m3EnableValidation
    m3_Free(i_runtime->validator);
#endif

#if d_m3HasStackSwitching
    Continuation_ReleaseAll(i_runtime);
#endif
}


void m3_FreeRuntime (IM3Runtime i_runtime)
{
    if (i_runtime) {
        m3_PrintProfilerInfo();

        Runtime_Release(i_runtime);
        m3_Free(i_runtime);
    }
}


#if d_m3HasStackSwitching

IM3Continuation Continuation_New (IM3Runtime i_runtime, IM3FuncType i_type, IM3Function i_function)
{
    IM3Continuation cont = m3_AllocStruct(M3Continuation);
    if (!cont) {
        return NULL;
    }

    cont->type          = i_type;
    cont->entryFunction = i_function;
    cont->state         = cont_allocated;

    cont->numStackSlots = d_m3ContinuationStackSlots;
    cont->valStack      = m3_AllocArray(m3slot_t, cont->numStackSlots + 4);
    if (!cont->valStack) {
        m3_Free(cont);
        return NULL;
    }

    // frames are recorded only when this continuation actually suspends, and
    // only as deep as it got, so the array starts out unallocated
    cont->frames    = NULL;
    cont->framesCap = 0;
    cont->numFrames = 0;

    cont->sp = cont->valStack;
    cont->pc = i_function ? i_function->compiled : NULL;
    cont->r0 = 0;
#  if d_m3HasFloat
    cont->fp0 = 0;
#  endif
    cont->boundArgsCount    = 0;
    cont->numSuspendResults = 0;
    cont->numHandlers       = 0;
    cont->handlersPC        = NULL;
    cont->parent            = NULL;

    cont->next               = i_runtime->continuations;
    i_runtime->continuations = cont;

    return cont;
}

IM3Continuation Continuation_ForkSuspended (IM3Runtime i_runtime, IM3Continuation i_cont)
{
    IM3Continuation newCont = Continuation_New(i_runtime, i_cont->type, i_cont->entryFunction);
    if (M3_UNLIKELY(not newCont)) {
        return NULL;
    }

    m3_Free(newCont->valStack);
    m3_Free(newCont->frames);

    newCont->valStack      = i_cont->valStack;
    newCont->numStackSlots = i_cont->numStackSlots;
    newCont->sp            = i_cont->sp;
    newCont->pc            = i_cont->pc;
    newCont->r0            = i_cont->r0;
#  if d_m3HasFloat
    newCont->fp0 = i_cont->fp0;
#  endif
    newCont->frames    = i_cont->frames;
    newCont->framesCap = i_cont->framesCap;
    newCont->numFrames = i_cont->numFrames;

    newCont->boundArgsCount = 0;
#  if d_m3HasExceptionHandling
    newCont->resumeThrow = i_cont->resumeThrow;
    i_cont->resumeThrow  = NULL;
#  endif
    newCont->numSuspendResults = i_cont->numSuspendResults;
    for (u32 i = 0; i < i_cont->numSuspendResults; ++i) {
        newCont->suspendResultOffsets[i] = i_cont->suspendResultOffsets[i];
        newCont->suspendResultIs64[i]    = i_cont->suspendResultIs64[i];
    }

    newCont->numHandlers = i_cont->numHandlers;
    newCont->handlersPC  = i_cont->handlersPC;
    newCont->parent      = i_cont->parent;
    newCont->state       = cont_suspended;

    i_cont->valStack  = NULL;
    i_cont->frames    = NULL;
    i_cont->framesCap = 0;
    i_cont->numFrames = 0;
    i_cont->state     = cont_consumed;

    return newCont;
}

// Continuations live until the runtime does. They are one-shot, but a resumed
// one is still reachable from the Wasm stack that holds its reference, and the
// engine has no way to know when the last copy is gone.
void Continuation_ReleaseAll (IM3Runtime io_runtime)
{
    IM3Continuation cont = io_runtime->continuations;

    while (cont) {
        IM3Continuation next = cont->next;

        m3_Free(cont->valStack);
        m3_Free(cont->frames);
        m3_Free(cont);

        cont = next;
    }

    io_runtime->continuations      = NULL;
    io_runtime->activeContinuation = NULL;
    io_runtime->rootContinuation   = NULL;
}


m3ret_t Continuation_RecordFrame (IM3Runtime i_runtime, const M3Frame* i_frame)
{
    IM3Continuation cont = i_runtime->activeContinuation;

    // the marker only travels between a suspend and the resume that runs it,
    // and that whole stretch has an active continuation
    d_m3Assert(cont);

    if (cont->numFrames == cont->framesCap) {
        u32 newCap = cont->framesCap ? cont->framesCap * 2 : 8;

        if (newCap > d_m3ContinuationMaxFrames) {
            newCap = d_m3ContinuationMaxFrames;
        }

        if (newCap == cont->framesCap) {
            return m3Err_trapStackOverflow;
        }

        M3Frame* frames = m3_ReallocArray(M3Frame, cont->frames, newCap, cont->framesCap);

        if (not frames) {
            return m3Err_mallocFailed;
        }

        cont->frames    = frames;
        cont->framesCap = newCap;
    }

    cont->frames[cont->numFrames++] = *i_frame;

    return m3Err_continuationSuspended;
}

#endif // d_m3HasStackSwitching

M3Result EvaluateExpression (IM3Module i_module, void* o_expressed, m3type_t i_type, bytes_t* io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_none;

    // OPTZ: use a simplified interpreter for expressions

    // create a temporary runtime context
#if defined(d_m3PreferStaticAlloc)
    static M3Runtime runtime;
#else
    M3Runtime runtime;
#endif
    M3_INIT(runtime);

    runtime.environment   = i_module->runtime->environment;
    runtime.numStackSlots = i_module->runtime->numStackSlots;
    runtime.stack         = i_module->runtime->stack;

    m3stack_t stack = (m3stack_t)runtime.stack;

    IM3Runtime savedRuntime = i_module->runtime;
    i_module->runtime       = &runtime;

    IM3Compilation o   = &runtime.compilation;
    o->runtime         = &runtime;
    o->module          = i_module;
    o->wasm            = *io_bytes;
    o->wasmEnd         = i_end;
    o->lastOpcodeStart = o->wasm;

    // Borrowed for the length of this expression only. Runtime_Release below hands it
    // to Environment_ReleaseCodePages, which resets lineIndex, so the page comes back
    // empty and the next expression reuses it rather than burning fresh lines.
    o->page = AcquireCodePage(&runtime);

    if (o->page) {
        IM3FuncType ftype = NULL;

#if d_m3HasTypedRefs
        // A reference type has to be checked as the type it is: the shared
        // return types are one per storage class, so borrowing one of those
        // would let any reference at all satisfy a particular one.
        if (IsSpelledRefType(i_type)) {
            result = AllocFuncType(&ftype, 1);

            if (not result) {
                ftype->numRets  = 1;
                ftype->types[0] = i_type;

                result = Environment_AddFuncType(runtime.environment, &ftype);
            }
        } else
#endif
        {
            ftype = runtime.environment->retFuncTypes[BaseTypeOf(i_type)];
        }

        pc_t m3code = GetPagePC(o->page);

        if (not result) {
            result = CompileExpression(o, ftype);
        }

        if (not result && o->maxStackSlots >= runtime.numStackSlots) {
            result = m3Err_trapStackOverflow;
        }

        if (not result) {
#if (d_m3EnableOpProfiling || d_m3EnableOpTracing)
            m3ret_t r = RunCode(m3code, stack, NULL, d_m3OpDefaultArgs, d_m3BaseCstr);
#else
            m3ret_t r = RunCode(m3code, stack, NULL, d_m3OpDefaultArgs);
#endif

            if (r == 0) {                                                                   m3log (runtime, "expression result: %s", SPrintValue (stack, i_type));
                if (SizeOfType(BaseTypeOf(i_type)) == sizeof(u32)) {
                    *(u32*)o_expressed = *((u32*)stack);
                } else {
                    *(u64*)o_expressed = *((u64*)stack);
                }
            }
        }

        ReleaseCodePage(&runtime, o->page);
    } else {
        result = m3Err_mallocFailedCodePage;
    }

    runtime.originStack = NULL;        // prevent free(stack) in ReleaseRuntime
    Runtime_Release(&runtime);
    i_module->runtime = savedRuntime;

    *io_bytes = o->wasm;

    return result;
}


//---------------------------------------------------------------------------------------------------------------------------------
//  Linking a module's imports against the exports of the modules already loaded
//  into the same runtime, matched on the name a module was registered under.
//
//  This is best-effort: an import nothing satisfies is left alone rather than
//  rejected, because a host function may still be bound to it after the module
//  is loaded (m3_LinkRawFunction needs the runtime, so it cannot run earlier),
//  and because an unsatisfiable memory or global still has to be backed by
//  something for the module to be loadable at all.
//---------------------------------------------------------------------------------------------------------------------------------

// Whether an exporting memory or table satisfies what an import asks for. The
// exporter's *current* size is its minimum - one that has been grown satisfies
// a larger import than its declaration would - and it may be no less bounded.
static
bool LimitsSatisfy (u64 i_exportedSize, bool i_exportedHasMax, u64 i_exportedMax,
                    u64 i_importMin, bool i_importHasMax, u64 i_importMax)
{
    if (i_exportedSize < i_importMin) {
        return false;
    }

    if (i_importHasMax and (not i_exportedHasMax or i_exportedMax > i_importMax)) {
        return false;
    }

    return true;
}


static
IM3Function Module_FindExportedFunction (IM3Module i_module, cstr_t i_name)
{
    for (u32 i = 0; i < i_module->numFunctions; ++i) {
        IM3Function f = &i_module->functions[i];

        if (f->export_name and strcmp(f->export_name, i_name) == 0) {
            // A module that re-exports an import names the placeholder here.
            // Resolve to the function that actually runs, or a host-side call
            // would run it against the importing module's memory.
            return Function_Implementation(f);
        }
    }

    return NULL;
}


static
IM3Memory Module_FindExportedMemory (IM3Module i_module, cstr_t i_name)
{
    for (u32 i = 0; i < i_module->numMemories; ++i) {
        IM3Memory memory = i_module->memories[i];

        if (memory->exportName and strcmp(memory->exportName, i_name) == 0) {
            return memory;
        }
    }

    return NULL;
}


static
IM3Table Module_FindExportedTable (IM3Module i_module, cstr_t i_name)
{
    for (u32 i = 0; i < i_module->numTables; ++i) {
        IM3Table table = i_module->tables[i];

        if (table->exportName and strcmp(table->exportName, i_name) == 0) {
            return table;
        }
    }

    return NULL;
}


static
IM3Global Module_FindExportedGlobal (IM3Module i_module, cstr_t i_name)
{
    for (u32 i = 0; i < i_module->numGlobals; ++i) {
        IM3Global g = &i_module->globals[i];

        if (g->name and strcmp(g->name, i_name) == 0) {
            return g;
        }
    }

    return NULL;
}


#if d_m3HasExceptionHandling || d_m3HasStackSwitching
static
IM3Tag Module_FindExportedTag (IM3Module i_module, cstr_t i_name)
{
    for (u32 i = 0; i < i_module->numTags; ++i) {
        IM3Tag tag = &i_module->tags[i];

        if (tag->name and strcmp(tag->name, i_name) == 0) {
            return tag->resolved ? tag->resolved : tag;
        }
    }

    return NULL;
}
#endif


// Whether the module exports anything at all under this name. Export names are
// unique within a module, so a name one of the lookups above missed but this
// one finds is exported as something else - a kind the import cannot be
// satisfied by, rather than a name the module never exported.
static
bool Module_HasExport (IM3Module i_module, cstr_t i_name)
{
    for (u32 i = 0; i < i_module->numFunctions; ++i) {
        IM3Function f = &i_module->functions[i];

        if (f->export_name and strcmp(f->export_name, i_name) == 0) {
            return true;
        }
    }

    for (u32 i = 0; i < i_module->numMemories; ++i) {
        IM3Memory memory = i_module->memories[i];

        if (memory->exportName and strcmp(memory->exportName, i_name) == 0) {
            return true;
        }
    }

    for (u32 i = 0; i < i_module->numTables; ++i) {
        IM3Table table = i_module->tables[i];

        if (table->exportName and strcmp(table->exportName, i_name) == 0) {
            return true;
        }
    }

    for (u32 i = 0; i < i_module->numGlobals; ++i) {
        IM3Global g = &i_module->globals[i];

        if (g->name and strcmp(g->name, i_name) == 0) {
            return true;
        }
    }

#if d_m3HasExceptionHandling || d_m3HasStackSwitching
    for (u32 i = 0; i < i_module->numTags; ++i) {
        IM3Tag tag = &i_module->tags[i];

        if (tag->name and strcmp(tag->name, i_name) == 0) {
            return true;
        }
    }
#endif

    return false;
}


// Points each of the module's imports at whatever already-loaded module exports
// it. Runs before anything is allocated or initialized: a memory import has to
// be resolved before InitMemory would give it pages of its own, and a global
// import before InitGlobals runs an initializer that reads it.
static
M3Result LinkImports (IM3Runtime io_runtime, IM3Module io_module)
{
    M3Result result = m3Err_none;

    for (u32 i = 0; i < io_module->numFunctions; ++i) {
        IM3Function f = &io_module->functions[i];

        if (f->wasm or not (f->import.moduleUtf8 and f->import.fieldUtf8)) {
            continue;
        }

        IM3Module from = m3_FindModule(io_runtime, f->import.moduleUtf8);
        if (not from) {
            continue;
        }

        IM3Function exported = Module_FindExportedFunction(from, f->import.fieldUtf8);
        if (not exported) {
            // the import names a module that is loaded, so its exports settle the
            // question: a name it exports as something else is a type mismatch, and
            // one it does not export at all is an unknown import
            _throwif(m3Err_incompatibleImportType, Module_HasExport(from, f->import.fieldUtf8));
            _throw(m3Err_unknownImport);
        }

        // func types are canonical within an environment, so this is the
        // structural equivalence the spec asks for
        _throwif(m3Err_incompatibleImportType, exported->funcType != f->funcType);

        f->resolved = Function_Implementation(exported);
    }

    for (u32 i = 0; i < io_module->numMemories; ++i) {
        IM3Memory memory = io_module->memories[i];

        if (not memory->imported or memory->owner != io_module) {
            continue;
        }

        IM3Module from = m3_FindModule(io_runtime, memory->import.moduleUtf8);
        if (not from) {
            continue;
        }

        IM3Memory exported = Module_FindExportedMemory(from, memory->import.fieldUtf8);
        if (not exported) {
            _throwif(m3Err_incompatibleImportType, Module_HasExport(from, memory->import.fieldUtf8));
            _throw(m3Err_unknownImport);
        }

        // the address type is part of the memory type, so an i64 memory does
        // not satisfy an i32 import, or the other way round - and so is the page
        // size, which custom page sizes made a declared property of a memory
        _throwif(m3Err_incompatibleImportType, exported->isMemory64 != memory->isMemory64);
        _throwif(m3Err_incompatibleImportType, Memory_PageSize(exported) != Memory_PageSize(memory));

        _throwif(m3Err_incompatibleImportType,
                 not LimitsSatisfy(exported->numPages, exported->hasMax, exported->maxPages,
                                   memory->initPages, memory->hasMax, memory->maxPages));

        // hand the slot over to the exporter's memory, and drop the placeholder
        FreeMemoryBlock(memory);
        m3_Free(memory->exportName);
        FreeImportInfo(&memory->import);
        m3_Free(memory);

        io_module->memories[i] = exported;
    }

    for (u32 i = 0; i < io_module->numTables; ++i) {
        IM3Table table = io_module->tables[i];

        if (not table->imported or table->owner != io_module) {
            continue;
        }

        IM3Module from = m3_FindModule(io_runtime, table->import.moduleUtf8);
        if (not from) {
            continue;
        }

        IM3Table exported = Module_FindExportedTable(from, table->import.fieldUtf8);
        if (not exported) {
            _throwif(m3Err_incompatibleImportType, Module_HasExport(from, table->import.fieldUtf8));
            _throw(m3Err_unknownImport);
        }

        _throwif(m3Err_incompatibleImportType, exported->type != table->type);

        // the index type is part of the table type, the same way it is for a memory
        _throwif(m3Err_incompatibleImportType, exported->isTable64 != table->isTable64);

        _throwif(m3Err_incompatibleImportType,
                 not LimitsSatisfy(exported->size, exported->hasMax, exported->maxSize,
                                   table->initSize, table->hasMax, table->maxSize));

        // hand the slot over to the exporter's table, and drop the placeholder
        m3_Free(table->elements);
        m3_Free(table->exportName);
        FreeImportInfo(&table->import);
        m3_Free(table);

        io_module->tables[i] = exported;
    }

    for (u32 i = 0; i < io_module->numGlobals; ++i) {
        IM3Global g = &io_module->globals[i];

        if (not g->imported or not (g->import.moduleUtf8 and g->import.fieldUtf8)) {
            continue;
        }

        IM3Module from = m3_FindModule(io_runtime, g->import.moduleUtf8);
        if (not from) {
            continue;
        }

        IM3Global exported = Module_FindExportedGlobal(from, g->import.fieldUtf8);
        if (not exported) {
            _throwif(m3Err_incompatibleImportType, Module_HasExport(from, g->import.fieldUtf8));
            _throw(m3Err_unknownImport);
        }

        _throwif(m3Err_incompatibleImportType, exported->type != g->type);
        _throwif(m3Err_incompatibleImportType, exported->isMutable != g->isMutable);

        g->resolved = exported->resolved ? exported->resolved : exported;
    }

#if d_m3HasExceptionHandling || d_m3HasStackSwitching
    for (u32 i = 0; i < io_module->numTags; ++i) {
        IM3Tag tag = &io_module->tags[i];

        if (not tag->imported or not (tag->import.moduleUtf8 and tag->import.fieldUtf8)) {
            continue;
        }

        IM3Module from = m3_FindModule(io_runtime, tag->import.moduleUtf8);
        if (not from) {
            continue;
        }

        IM3Tag exported = Module_FindExportedTag(from, tag->import.fieldUtf8);
        if (not exported) {
            _throwif(m3Err_incompatibleImportType, Module_HasExport(from, tag->import.fieldUtf8));
            _throw(m3Err_unknownImport);
        }

        _throwif(m3Err_incompatibleImportType, exported->type != tag->type);

        tag->resolved = exported->resolved ? exported->resolved : exported;
    }
#endif

    _catch: return result;
}


// Backs each of the module's memories with pages. LinkImports has already
// pointed any import it could satisfy at the exporting module's memory, and
// those are skipped here. An import nothing satisfied is still backed locally
// from its own declared limits, so that the module remains loadable.
M3Result InitMemory (IM3Runtime io_runtime, IM3Module i_module)
{
    M3Result result = m3Err_none;

    // Fixed from here on: the index space stops changing after parse, and
    // linking has already repointed any slot it was going to.
    i_module->memory0 = i_module->numMemories ? i_module->memories[0]
                                              : &i_module->emptyMemory;

    if (i_module->numMemories == 0) {
        // nothing addressable, but _mem still has to point somewhere
        i_module->emptyMemory.owner    = i_module;
        i_module->emptyMemory.pageSize = d_m3DefaultMemPageSize;

_       (ResizeMemory(io_runtime, &i_module->emptyMemory, 0));
    }

    for (u32 i = 0; i < i_module->numMemories; ++i) {
        IM3Memory memory = i_module->memories[i];

        // a slot that already points at another module's memory is that
        // module's to allocate
        if (memory->owner != i_module or memory->mallocated) {
            continue;
        }

        u32 pageSize = Memory_PageSize(memory);

        memory->pageSize = pageSize;

        // Without a declared maximum a memory may grow to the spec limit of
        // 2^|addrtype|/pagesize pages, which is the usual 65536 at the default
        // page size, 2^48 for a 64-bit memory, and a whole address space of
        // them when a page is a single byte. A declared maximum of zero is a
        // real limit, not the absence of one.
        //
        // 2^64/pagesize overflows a u64 only when a page is a single byte, and
        // a power-of-two page size divides the address space exactly, so the
        // 64-bit division below is 2^64/pagesize written so that it fits.
        if (not memory->hasMax) {
            memory->maxPages = memory->isMemory64
                                 ? (pageSize > 1 ? (UINT64_MAX / pageSize) + 1 : UINT64_MAX)
                                 : (0x100000000ull / pageSize);
        }

_       (ResizeMemory(io_runtime, memory, memory->initPages));
    }

    _catch: return result;
}


M3Result ResizeMemory (IM3Runtime io_runtime, IM3Memory memory, u64 i_numPages)
{
    M3Result result = m3Err_none;

    u64 numPagesToAlloc = i_numPages;

    if (numPagesToAlloc <= memory->maxPages) {
        // A 64-bit memory may ask for up to 2^48 pages, which overflows a u64
        // of bytes. Nothing that large can be backed, so refuse it up front
        // rather than multiplying into a wrapped size.
        _throwif("linear memory limitation exceeded",
                 numPagesToAlloc > d_m3AddressLimit / memory->pageSize);

        u64 numPageBytes = numPagesToAlloc * memory->pageSize;

#if d_m3MaxLinearMemoryPages > 0
        // the limit is a memory size, counted in default-sized pages; comparing
        // it against a raw page count would make it 65536 times stricter for a
        // module whose pages are one byte
        _throwif("linear memory limitation exceeded",
                 numPageBytes > (u64)d_m3MaxLinearMemoryPages * d_m3DefaultMemPageSize);
#endif

        // Limit the amount of memory that gets actually allocated
        if (io_runtime->memoryLimit) {
            numPageBytes = M3_MIN(numPageBytes, (u64)io_runtime->memoryLimit);
        }

        _throwif("linear memory limitation exceeded", numPageBytes > (u64)SIZE_MAX - sizeof(M3MemoryHeader));

#if d_m3GuardedMemory

        // A slot holds every address a Wasm access can name, and nothing beyond
        // that: a memory larger than the data part of one would put addresses past
        // the guard, where a fault is nobody's to recognize
        _throwif("linear memory limitation exceeded", numPageBytes > d_m3GuardedDataBytes);

        // What makes an access past the end fault is the page after the memory not
        // being committed, so the end has to fall on one of the system's page
        // boundaries. A Wasm page divides evenly into them and always will; a memory
        // that declared a page size of its own may not, and then the bytes between
        // the end of the memory and the end of its last system page would answer
        // instead of trapping. Refusing to back such a memory is the honest way out:
        // the custom page sizes proposal exists for targets far too small to reserve
        // 8GiB per memory in the first place. See d_m3GuardedMemory.
        _throwif("guarded memory needs a page size the system's divides into",
                 (memory->pageSize % m3_HostPageSize()) != 0);

        if (memory->guardSlot == NULL) {
            memory->guardSlot = Guard_TakeSlot();
            _throwifnull(memory->guardSlot);
        }

        // Growing only ever commits more of the same slot, so the memory never
        // moves - which the interpreter does not rely on, but nothing minds
        _throwif(m3Err_mallocFailed, not Guard_CommitSlot(memory->guardSlot, (size_t)numPageBytes));

        memory->mallocated = Guard_SlotHeader(memory->guardSlot);

#else

        size_t numBytes = (size_t)numPageBytes + sizeof(M3MemoryHeader);

        // What is really there, which is not numPages * pageSize: the limit above
        // caps the bytes without capping the page count, so a memory can be short
        // of a page - by design, since a whole 64 KiB page is often more than an
        // MCU has. Measuring the old block by the page count would overstate it,
        // and then realloc never sees the size it already has and copies the
        // whole limit again on every grow past it.
        size_t numPreviousBytes = memory->mallocated ? memory->mallocated->length + sizeof(M3MemoryHeader) : 0;

        void* newMem = m3_Realloc("Wasm Linear Memory", memory->mallocated, numBytes, numPreviousBytes);
        _throwifnull(newMem);

        memory->mallocated = (M3MemoryHeader*)newMem;

#endif

#if d_m3LogRuntime
        M3MemoryHeader* oldMallocated = memory->mallocated;
#endif

        memory->numPages = numPagesToAlloc;

        memory->mallocated->length  = (size_t)numPageBytes;
        memory->mallocated->runtime = io_runtime;
        memory->mallocated->memory  = memory;

        memory->mallocated->maxStack = (m3slot_t*)io_runtime->originStack + io_runtime->numStackSlots;

        m3log(runtime, "resized old: %p; mem: %p; length: %zu; pages: %llu", oldMallocated, memory->mallocated, memory->mallocated->length, (unsigned long long)memory->numPages);
    } else {
        result = m3Err_wasmMemoryOverflow;
    }

    _catch: return result;
}


void FreeMemoryBlock (IM3Memory io_memory)
{
#if d_m3GuardedMemory
    Guard_GiveSlot(io_memory->guardSlot);
    io_memory->guardSlot = NULL;

    io_memory->mallocated = NULL;
#else
    m3_Free(io_memory->mallocated);
#endif
}


M3Result InitGlobals (IM3Module io_module)
{
    M3Result result = m3Err_none;

    if (io_module->numGlobals) {
        // placing the globals in their structs isn't good for cache locality, but i don't really know what the global
        // access patterns typically look like yet.

        //          io_module->globalMemory = m3Alloc (m3reg_t, io_module->numGlobals);

        //          if (io_module->globalMemory)
        {
            for (u32 i = 0; i < io_module->numGlobals; ++i) {
                M3Global* g = &io_module->globals[i];                           m3log (runtime, "initializing global: %d", i);

                if (g->initExpr) {
                    bytes_t start = g->initExpr;

                    result = EvaluateExpression(io_module, &g->i64Value, g->type, &start, g->initExpr + g->initExprSize);

                    if (not result) {
                        // io_module->globalMemory [i] = initValue;
                    } else {
                        break;
                    }
                } else {                                                        m3log (runtime, "importing global");
                }
            }
        }
        //          else result = ErrorModule (m3Err_mallocFailed, io_module, "could allocate globals for module: '%s", io_module->name);
    }

    return result;
}


M3Result InitDataSegments (IM3Module io_module)
{
    M3Result result = m3Err_none;

    for (u32 i = 0; i < io_module->numDataSegments; ++i) {
        M3DataSegment* segment = &io_module->dataSegments[i];

        // A passive segment stays available for memory.init until data.drop.
        // An active one is copied here and then counts as dropped.
        if (segment->isPassive) {
            continue;
        }

        _throwif("data segment memory index out of range",
                 segment->memoryRegion >= io_module->numMemories);

        IM3Memory io_memory = io_module->memories[segment->memoryRegion];

        _throwif("unallocated linear memory", !(io_memory->mallocated));

        // The offset expression has the memory's address type, and is
        // unsigned: an i32 offset of -1 is 4294967295, way out of bounds
        // rather than negative.
        u64     segmentOffset = 0;
        bytes_t start         = segment->initExpr;

        if (io_memory->isMemory64) {
_           (EvaluateExpression(io_module, &segmentOffset, c_m3Type_i64, &start, segment->initExpr + segment->initExprSize));
        } else {
            u32 offset32;
_           (EvaluateExpression(io_module, &offset32, c_m3Type_i32, &start, segment->initExpr + segment->initExprSize));
            segmentOffset = offset32;
        }

        m3log(runtime, "loading data segment: %d; size: %d; offset: %llu", i, segment->size, (unsigned long long)segmentOffset);

        if (segmentOffset <= io_memory->mallocated->length &&
            (u64)segment->size <= io_memory->mallocated->length - segmentOffset) {
            u8* dest = m3MemData(io_memory->mallocated) + segmentOffset;
            memcpy(dest, segment->data, segment->size);
        } else {
            _throw("data segment out of bounds");
        }

        segment->dropped = true;
    }

    _catch: return result;
}


// Turns a segment's elements into references. Element expressions are constant
// expressions restricted to ref.null/ref.func, so they're read directly rather
// than run through the compiler.
static
M3Result ResolveElements (IM3Module io_module, M3ElementSegment* i_segment, void** o_elements)
{
    M3Result result = m3Err_none;

    bytes_t  pos = i_segment->elements;
    cbytes_t end = io_module->elementSectionEnd;

    for (u32 e = 0; e < i_segment->numElements; ++e) {
        u32   funcIndex;
        void* ref = NULL;

        if (i_segment->isExpr) {
            m3opcode_t opcode;
_           (Read_opcode(&opcode, &pos, end));

            if (opcode == c_waOp_refFunc) {
_               (ReadLEB_u32(&funcIndex, &pos, end));
                _throwif("function index out of range", funcIndex >= io_module->numFunctions);
                ref = Function_Implementation(&io_module->functions[funcIndex]);
            } else if (opcode == c_waOp_refNull) {
                i8 waType;
                u8 nullType;
_               (ReadLEB_i7(&waType, &pos, end));
_               (NormalizeType(&nullType, waType));
                _throwif(m3Err_typeMismatch, nullType != i_segment->type);
            } else if (opcode == c_waOp_getGlobal) {
                // wasm 2.0 lets an element expression read an imported
                // immutable global, which is how one module seeds another's
                // table with a reference it exported.
                u32 globalIndex;
_               (ReadLEB_u32(&globalIndex, &pos, end));
                _throwif(m3Err_globaIndexOutOfBounds, globalIndex >= io_module->numGlobals);

                IM3Global global = &io_module->globals[globalIndex];

                _throwif(m3Err_globaIndexOutOfBounds, not global->imported);
                _throwif(m3Err_wasmMalformed, global->isMutable);
                _throwif(m3Err_typeMismatch, BaseTypeOf(global->type) != BaseTypeOf(i_segment->type));

                // read the cell the import was linked to, not the placeholder
                if (global->resolved) {
                    global = global->resolved;
                }

                ref = global->refValue;
            } else {
                _throw("constant expression required");
            }

_           (Read_opcode(&opcode, &pos, end));
            _throwif(m3Err_wasmMalformed, opcode != c_waOp_end);
        } else {
_           (ReadLEB_u32(&funcIndex, &pos, end));
            _throwif("function index out of range", funcIndex >= io_module->numFunctions);
            ref = Function_Implementation(&io_module->functions[funcIndex]);
        }

        o_elements[e] = ref;
    }

    _catch: return result;
}


M3Result InitTableAndElements (IM3Module io_module)
{
    M3Result result = m3Err_none;

    cbytes_t end = io_module->elementSectionEnd;
    M3Table* table;

    for (u32 i = 0; i < io_module->numTables; ++i) {
        table = io_module->tables[i];

        // a slot pointing at another module's table is that module's to fill
        if (table->owner != io_module) {
            continue;
        }

        if (table->size) {
            table->elements = m3_AllocArray(void*, table->size);
            _throwifnull(table->elements);

            if (table->initExpr) {
                void*   value = NULL;
                bytes_t start = table->initExpr;
_               (EvaluateExpression(io_module, &value, BaseTypeOf(table->type),
                                     &start, table->initExpr + table->initExprSize));

                for (u32 e = 0; e < table->size; ++e) {
                    table->elements[e] = value;
                }
            }
        }
    }

    for (u32 i = 0; i < io_module->numElementSegments; ++i) {
        M3ElementSegment* segment = &io_module->elementSegments[i];

        // Declarative segments only make their functions referenceable, and
        // passive ones wait for table.init, so neither is written out here.
        if (segment->mode == c_m3Elem_declarative) {
            segment->dropped = true;
            continue;
        }

        if (segment->mode == c_m3Elem_passive) {
            if (segment->numElements) {
                segment->resolved = m3_AllocArray(void*, segment->numElements);
                _throwifnull(segment->resolved);
_               (ResolveElements(io_module, segment, segment->resolved));
            }
            continue;
        }

        table = io_module->tables[segment->tableIndex];

        // The offset expression has the table's index type, and is unsigned:
        // an i32 offset of -1 is 4294967295, out of bounds rather than negative.
        u64     offset = 0;
        bytes_t expr   = segment->initExpr;

        if (table->isTable64) {
_           (EvaluateExpression(io_module, &offset, c_m3Type_i64, &expr, end));
        } else {
            u32 offset32;
_           (EvaluateExpression(io_module, &offset32, c_m3Type_i32, &expr, end));
            offset = offset32;
        }

        _throwif("out of bounds table access",
                 offset > table->size or segment->numElements > table->size - offset);

_       (ResolveElements(io_module, segment, table->elements + offset));

        segment->dropped = true;
    }

    _catch: return result;
}

M3Result m3_CompileModule (IM3Module io_module)
{
    M3Result result = m3Err_none;

    for (u32 i = 0; i < io_module->numFunctions; ++i) {
        IM3Function f = &io_module->functions[i];
        if (f->wasm and not f->compiled) {
_           (CompileFunction(f));
        }
    }

    _catch: return result;
}

#if d_m3HasExceptionHandling

M3Exception* NewException (IM3Runtime io_runtime, IM3Tag i_tag, u32 i_numArgs)
{
    M3Exception* exception = (M3Exception*)m3_Malloc("M3Exception", sizeof(M3Exception) + i_numArgs * sizeof(u64));

    if (exception) {
        exception->tag     = i_tag;
        exception->numArgs = i_numArgs;
        exception->reified = false;
        exception->prev    = NULL;
        exception->next    = io_runtime->exceptions;

        if (exception->next) {
            exception->next->prev = exception;
        }

        io_runtime->exceptions = exception;
    }

    return exception;
}


// Releases one exception ahead of the rest. The caller has to know nothing can
// still name it: no exnref was ever taken of it, and its payload has already
// been copied out.
void FreeException (IM3Runtime io_runtime, M3Exception* i_exception)
{
    if (i_exception->prev) {
        i_exception->prev->next = i_exception->next;
    } else {
        io_runtime->exceptions = i_exception->next;
    }

    if (i_exception->next) {
        i_exception->next->prev = i_exception->prev;
    }

    if (io_runtime->pendingException == i_exception) {
        io_runtime->pendingException = NULL;
    }

    m3_Free_Impl(i_exception);
}


// Releases every exception the runtime still holds. Only safe once the Wasm
// stack is empty, which is why the outermost RunCodeChecked() is the one that
// calls it.
void FreeExceptions (IM3Runtime io_runtime)
{
    M3Exception* exception = io_runtime->exceptions;

    io_runtime->exceptions       = NULL;
    io_runtime->pendingException = NULL;

    while (exception) {
        M3Exception* next = exception->next;
        m3_Free_Impl(exception);
        exception = next;
    }
}

#endif // d_m3HasExceptionHandling


#if d_m3GuardedMemory

// What RunCodeProtected hands to the platform layer to run, and what comes back
typedef struct M3ProtectedRun {
    pc_t            pc;
    IM3Runtime      runtime;
    M3MemoryHeader* mem;
    M3Result        result;
} M3ProtectedRun;

static
void RunCodeBody (void* io_context)
{
    M3ProtectedRun* run = (M3ProtectedRun*)io_context;

#  if (d_m3EnableOpProfiling || d_m3EnableOpTracing)
    run->result = (M3Result)RunCode(run->pc, (m3stack_t)run->runtime->stack, run->mem,
                                    d_m3OpDefaultArgs, d_m3BaseCstr);
#  else
    run->result = (M3Result)RunCode(run->pc, (m3stack_t)run->runtime->stack, run->mem,
                                    d_m3OpDefaultArgs);
#  endif
}

// The bounds check the load and store operations no longer do, done once here by the
// system instead: an address past the end of a memory reaches the uncommitted part
// of that memory's reservation, and the fault comes back as the trap the spec asks
// for. Nothing is left half-done by it - the Wasm stack and the memories are heap,
// and the caller unwinds the rest.
static
M3Result RunCodeProtected (IM3Runtime i_runtime, pc_t i_pc, M3MemoryHeader* i_mem)
{
    // Memory operations in this build carry no bounds check, so code only runs where a
    // fault past the end of a memory can still be turned into a trap
    if (M3_UNLIKELY(not m3_HostGuardsActive())) {
        return ErrorRuntime(m3Err_memoryGuardsUnavailable, i_runtime,
                            "another part of the process holds the fault signal");
    }

    M3ProtectedRun run;
    run.pc      = i_pc;
    run.runtime = i_runtime;
    run.mem     = i_mem;
    run.result  = m3Err_none;

    void*  arena;
    size_t arenaBytes;
    Guard_ArenaRange(&arena, &arenaBytes);

    if (not m3_HostProtectedCall(RunCodeBody, &run, arena, arenaBytes)) {
        return m3Err_trapOutOfBoundsMemoryAccess;
    }

    return run.result;
}

#endif // d_m3GuardedMemory


#if d_m3HasStackSwitching

// The function a suspended continuation stopped in: the callee of its innermost
// call frame, or the function it was entered with if it made no call. Frames are
// recorded innermost first, so that is the lowest-numbered call frame.
static
IM3Function Continuation_InnermostFunction (IM3Continuation i_cont)
{
    for (u32 i = 0; i < i_cont->numFrames; ++i) {
        if (i_cont->frames[i].kind == frame_call && i_cont->frames[i].call.function) {
            return i_cont->frames[i].call.function;
        }
    }

    return i_cont->entryFunction;
}


// Where the host's next call puts its arguments and finds its results. A
// suspended root keeps its frames on the bottom of the runtime's stack, so a call
// made before it is resumed starts above the last of them rather than on top of
// them. The top is the innermost function's frame base plus everything that
// function can use. That base is the root's own sp, unless it stopped inside a
// resume: then the suspension point belongs to the resumed continuation, and the
// root's innermost frame is that resume.
static
void Runtime_PlaceCallStack (IM3Runtime io_runtime)
{
    IM3Continuation root = io_runtime->rootContinuation;

    io_runtime->stack = io_runtime->originStack;

    if (not root or root->state != cont_suspended) {
        return;
    }

    m3stack_t   sp       = root->sp;
    IM3Function function = Continuation_InnermostFunction(root);

    if (root->numFrames and root->frames[0].kind == frame_resume) {
        sp = root->frames[0].sp;
    }

    m3slot_t* base = (m3slot_t*)io_runtime->originStack;
    m3slot_t* top  = (m3slot_t*)sp + (function ? function->maxStackSlots : 0);

    if (top > base + io_runtime->numStackSlots) {
        top = base + io_runtime->numStackSlots;
    }

    io_runtime->stack = top;
}

#endif // d_m3HasStackSwitching


// Run compiled code on the runtime's stack, bounding native recursion for the
// duration of the call. The outermost invocation establishes the stack limit;
// nested ones (an imported function calling back into Wasm) inherit it.
static inline
M3Result RunCodeChecked (IM3Runtime i_runtime, IM3Function i_function)
{
    pc_t i_pc = i_function->compiled;

    // execution runs against the memory of the module the entry point belongs
    // to, not against some runtime-wide one
    M3MemoryHeader* _mem = Module_MemoryHeader(i_function->module);

    d_m3StackLimitEnter(i_runtime);

    // a host function calling back into Wasm does not change which call a trap
    // ends: that is still the one the host made first
    if (i_runtime->callNesting++ == 0) {
        i_runtime->entered = i_function;
    }
#if d_m3HasExceptionHandling
    // handler stacks don't nest across a call boundary: a host function calling
    // back into Wasm cannot be caught by a try_table its own caller entered
    u32 savedTryDepth   = i_runtime->tryDepth;
    i_runtime->tryDepth = 0;
#endif
#if d_m3HasStackSwitching
    // nor do continuations: the native frames on the far side of a host call
    // are not ours to record, so a suspend in here has no handler to reach and
    // no resume to unwind to
    IM3Continuation savedContinuation = i_runtime->activeContinuation;

    // A suspended root continuation is a paused program waiting to be resumed,
    // not scratch space. A call the host makes before resuming it gets a
    // context of its own, the way any other re-entry does, rather than having
    // the frames of the suspension reset out from under it.
    bool claimRoot = i_runtime->isSuspendable and not savedContinuation and
                     not (i_runtime->rootContinuation and
                          i_runtime->rootContinuation->state == cont_suspended);

    if (claimRoot and not i_runtime->rootContinuation) {
        i_runtime->rootContinuation = Continuation_New(i_runtime, i_function->funcType, i_function);

        if (i_runtime->rootContinuation) {
            // the root runs on the runtime's own stack rather than a buffer of
            // its own, so the one it was given is handed straight back
            m3_Free(i_runtime->rootContinuation->valStack);
            i_runtime->rootContinuation->numStackSlots = i_runtime->numStackSlots;
        } else {
            claimRoot = false;
        }
    }

    if (claimRoot) {
        i_runtime->activeContinuation              = i_runtime->rootContinuation;
        i_runtime->rootContinuation->entryFunction = i_function;
        i_runtime->rootContinuation->state         = cont_running;
        i_runtime->rootContinuation->numFrames     = 0;
    } else {
        i_runtime->activeContinuation = NULL;
    }
#endif
#if d_m3GuardedMemory
    M3Result result = RunCodeProtected(i_runtime, i_pc, _mem);
#elif (d_m3EnableOpProfiling || d_m3EnableOpTracing)
    M3Result result = (M3Result)RunCode(i_pc, (m3stack_t)i_runtime->stack, _mem, d_m3OpDefaultArgs, d_m3BaseCstr);
#else
    M3Result result = (M3Result)RunCode(i_pc, (m3stack_t)i_runtime->stack, _mem, d_m3OpDefaultArgs);
#endif
#if d_m3HasStackSwitching
    if (claimRoot) {
        i_runtime->rootContinuation->state =
          (result == m3Err_continuationSuspended) ? cont_suspended : cont_returned;

        Runtime_PlaceCallStack(i_runtime);
    }
    i_runtime->activeContinuation = savedContinuation;
#endif
#if d_m3HasExceptionHandling
    i_runtime->tryDepth = savedTryDepth;

    // an exception that reached the bottom of the call stack found no handler
    if (M3_UNLIKELY(result == m3Err_pendingException)) {
        result = m3Err_trapUncaughtException;
    }

    if (i_runtime->callNesting == 1) {
        FreeExceptions(i_runtime);
    }
#endif
    i_runtime->callNesting--;
    d_m3StackLimitLeave(i_runtime);

    return result;
}

M3Result m3_RunStart (IM3Module io_module)
{
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    // Execution disabled for fuzzing builds
    return m3Err_none;
#endif

    M3Result result           = m3Err_none;
    i32      startFunctionTmp = -1;

    if (io_module and io_module->startFunction >= 0) {
        IM3Function function = &io_module->functions[io_module->startFunction];

        if (not function->compiled) {
_           (CompileFunction(function));
        }

        IM3FuncType ftype = function->funcType;
        if (ftype->numArgs != 0 || ftype->numRets != 0) {
            _throw(m3Err_argumentCountMismatch);
        }

        IM3Module  module  = function->module;
        IM3Runtime runtime = module->runtime;

        startFunctionTmp = io_module->startFunction;

        io_module->startFunction = -1;

        result = RunCodeChecked(runtime, function);

        // a start function that suspended is not to be started again: resuming
        // is what finishes it
        if (result and result != m3Err_continuationSuspended) {
            io_module->startFunction = startFunctionTmp;
            EXCEPTION_PRINT(result);
            goto _catch;
        }
    }

    _catch: return result;
}

// TODO: deal with main + side-modules loading efforcement
M3Result m3_LoadModule (IM3Runtime io_runtime, IM3Module io_module)
{
    M3Result result = m3Err_none;

    if (M3_UNLIKELY(io_module->runtime)) {
        return m3Err_moduleAlreadyLinked;
    }

    io_module->runtime = io_runtime;

    // linking first: a memory import has to be resolved before InitMemory would
    // give it pages of its own, and a global import before an initializer reads it
_   (LinkImports(io_runtime, io_module));

_   (InitMemory(io_runtime, io_module));
_   (InitGlobals(io_module));
    // Spec order: element segments are applied before data segments. It matters
    // when one of them traps - whatever ran before the trap stays done.
_   (InitTableAndElements(io_module));
_   (InitDataSegments(io_module));

    // Start func might use imported functions, which are not liked here yet,
    // so it will be called before a function call is attempted (in m3_FindFunction)

#ifdef DEBUG
    Module_GenerateNames(io_module);
#endif

    io_module->next     = io_runtime->modules;
    io_runtime->modules = io_module;
    return result; // ok

_catch:
    // The runtime owns the module either way. Instantiation may already have
    // written this module's functions into a table another module owns, and the
    // spec keeps whatever it managed to do before the trap, so those entries
    // stay callable - retaining the module is what stops them dangling.
    //
    // It goes on the tail of the list rather than the head: it is not a module
    // anyone should find by name, only one whose functions may still be
    // reachable through someone else's table.
    io_module->next = NULL;

    IM3Module* tail = &io_runtime->modules;
    while (*tail) {
        tail = &(*tail)->next;
    }
    *tail = io_module;

    return result;
}

IM3Global m3_FindGlobal (IM3Module         io_module,
                         const char* const i_globalName)
{
    // Search exports
    for (u32 i = 0; i < io_module->numGlobals; ++i) {
        IM3Global g = &io_module->globals[i];
        if (g->name and strcmp(g->name, i_globalName) == 0) {
            // a re-exported global is the one that was imported, so reads and
            // writes have to reach the cell that actually holds the value
            return g->resolved ? g->resolved : g;
        }
    }

    // Search imports
    for (u32 i = 0; i < io_module->numGlobals; ++i) {
        IM3Global g = &io_module->globals[i];

        if (g->import.moduleUtf8 and g->import.fieldUtf8) {
            if (strcmp(g->import.fieldUtf8, i_globalName) == 0) {
                return g->resolved ? g->resolved : g;
            }
        }
    }
    return NULL;
}

M3Result m3_GetGlobal (IM3Global      i_global,
                       IM3TaggedValue o_value)
{
    if (not i_global) {
        return m3Err_globalLookupFailed;
    }

    switch (i_global->type) {
    case c_m3Type_i32: o_value->value.i32 = i_global->i32Value; break;
    case c_m3Type_i64: o_value->value.i64 = i_global->i64Value; break;
#if d_m3HasFloat
    case c_m3Type_f32: o_value->value.f32 = i_global->f32Value; break;
    case c_m3Type_f64: o_value->value.f64 = i_global->f64Value; break;
#endif
    default: return m3Err_invalidTypeId;
    }

    o_value->type = (M3ValueType)(i_global->type);
    return m3Err_none;
}

M3Result m3_SetGlobal (IM3Global            i_global,
                       const IM3TaggedValue i_value)
{
    if (not i_global) {
        return m3Err_globalLookupFailed;
    }
    if (not i_global->isMutable) {
        return m3Err_globalNotMutable;
    }
    if (i_global->type != i_value->type) {
        return m3Err_globalTypeMismatch;
    }

    switch (i_value->type) {
    case c_m3Type_i32: i_global->i32Value = i_value->value.i32; break;
    case c_m3Type_i64: i_global->i64Value = i_value->value.i64; break;
#if d_m3HasFloat
    case c_m3Type_f32: i_global->f32Value = i_value->value.f32; break;
    case c_m3Type_f64: i_global->f64Value = i_value->value.f64; break;
#endif
    default: return m3Err_invalidTypeId;
    }

    return m3Err_none;
}

M3ValueType m3_GetGlobalType (IM3Global i_global)
{
    return (i_global) ? (M3ValueType)(i_global->type) : c_m3Type_none;
}


void* v_FindFunction (IM3Module i_module, void* i_info)
{
    const char* const i_name = (const char*)i_info;

    // Prefer exported functions
    for (u32 i = 0; i < i_module->numFunctions; ++i) {
        IM3Function f = &i_module->functions[i];
        if (f->export_name and strcmp(f->export_name, i_name) == 0) {
            // A module that re-exports an import names the placeholder here.
            // Resolve to the function that actually runs, or a host-side call
            // would run it against the importing module's memory.
            return Function_Implementation(f);
        }
    }

    // Search internal functions
    for (u32 i = 0; i < i_module->numFunctions; ++i) {
        IM3Function f = &i_module->functions[i];

        bool isImported = f->import.moduleUtf8 or f->import.fieldUtf8;

        if (isImported) {
            continue;
        }

        for (int j = 0; j < f->numNames; j++) {
            if (f->names[j] and strcmp(f->names[j], i_name) == 0) {
                return f;
            }
        }
    }

    return NULL;
}


// Shared tail of the two lookups: a function is only usable once it has code.
static
M3Result PrepareFoundFunction (IM3Function* o_function, IM3Function i_function)
{
    M3Result result = m3Err_none;

    if (not i_function->compiled) {
_       (CompileFunction(i_function))
    }

    _catch:
    *o_function = result ? NULL : i_function;

    return result;
}


// Searches every module in the runtime, most recently loaded first. That is a
// guess once more than one module is loaded and two of them export the same
// name - m3_FindFunctionIn says which module is meant.
M3Result m3_FindFunction (IM3Function* o_function, IM3Runtime i_runtime, const char* const i_functionName)
{
                                                                d_m3Assert (o_function and i_runtime and i_functionName);
    IM3Function function = NULL;

    if (not i_runtime->modules) {
        *o_function = NULL;
        return "no modules loaded";
    }

    function = (IM3Function)ForEachModule(i_runtime, v_FindFunction, (void*)i_functionName);

    if (not function) {
        *o_function = NULL;
        return ErrorModule(m3Err_functionLookupFailed, i_runtime->modules, "'%s'", i_functionName);
    }

    return PrepareFoundFunction(o_function, function);
}


IM3Module m3_FindModule (IM3Runtime i_runtime, const char* const i_moduleName)
{
    if (not i_runtime or not i_moduleName) {
        return NULL;
    }

    // the list is newest-first, so a name registered twice names the newer one
    for (IM3Module m = i_runtime->modules; m; m = m->next) {
        if (m->name and strcmp(m->name, i_moduleName) == 0) {
            return m;
        }
    }

    return NULL;
}


// Searches one module's exports, which is what naming a module means.
M3Result m3_FindFunctionIn (IM3Function* o_function, IM3Module i_module, const char* const i_functionName)
{
                                                                d_m3Assert (o_function and i_functionName);
    if (not i_module) {
        *o_function = NULL;
        return m3Err_functionLookupFailed;      // ErrorModule would deref it
    }

    IM3Function function = (IM3Function)v_FindFunction(i_module, (void*)i_functionName);

    if (not function) {
        *o_function = NULL;
        return ErrorModule(m3Err_functionLookupFailed, i_module, "'%s'", i_functionName);
    }

    return PrepareFoundFunction(o_function, function);
}


M3Result m3_GetTableFunction (IM3Function* o_function, IM3Module i_module, uint32_t i_index)
{
_try {
    M3Table*    table;
    IM3Function function;

    _throwif("no table", i_module->numTables == 0);

    table = i_module->tables[0];
    _throwif("function index out of range", i_index >= table->size);

    function = (IM3Function)table->elements[i_index];

    if (function) {
        if (not function->compiled) {
_           (CompileFunction(function))
        }
    }

    *o_function = function;
} _catch:
    return result;
}


static
M3Result checkStartFunction (IM3Module i_module)
{
    M3Result result = m3Err_none;                               d_m3Assert(i_module);

    // Check if start function needs to be called
    if (i_module->startFunction >= 0) {
        result = m3_RunStart(i_module);
    }

    return result;
}

uint32_t m3_GetArgCount (IM3Function i_function)
{
    if (i_function) {
        IM3FuncType ft = i_function->funcType;
        if (ft) {
            return ft->numArgs;
        }
    }
    return 0;
}

uint32_t m3_GetRetCount (IM3Function i_function)
{
    if (i_function) {
        IM3FuncType ft = i_function->funcType;
        if (ft) {
            return ft->numRets;
        }
    }
    return 0;
}


M3ValueType m3_GetArgType (IM3Function i_function, uint32_t index)
{
    if (i_function) {
        IM3FuncType ft = i_function->funcType;
        if (ft and index < ft->numArgs) {
            return (M3ValueType)BaseTypeOf(d_FuncArgType(ft, index));
        }
    }
    return c_m3Type_none;
}

M3ValueType m3_GetRetType (IM3Function i_function, uint32_t index)
{
    if (i_function) {
        IM3FuncType ft = i_function->funcType;
        if (ft and index < ft->numRets) {
            return (M3ValueType)BaseTypeOf(d_FuncRetType(ft, index));
        }
    }
    return c_m3Type_none;
}


// The call APIs marshal their arguments into the runtime stack before any
// compiled code runs, so op_Entry's overflow check comes too late to cover those
// writes - and for a host function or an import linked to another module there
// is no op_Entry of our own to reach at all. Bound the area about to be written
// here instead: results and arguments each take a u64 at this boundary, however
// wide the slot is inside. Every caller goes through this, so none of them can
// be the one that forgets.
static
M3Result GetStackPointerForArgs (IM3Function i_function, u8** o_stack)
{
    IM3Runtime  runtime = i_function->module->runtime;
    IM3FuncType ftype   = i_function->funcType;

    // the stack a paused invocation holds is not available to this one
    size_t used      = (size_t)((u8*)runtime->stack - (u8*)runtime->originStack);
    size_t needed    = ((size_t)ftype->numRets + ftype->numArgs) * sizeof(u64);
    size_t available = (size_t)runtime->numStackSlots * sizeof(m3slot_t) - used;

    if (needed > available) {
        return m3Err_trapStackOverflow;
    }

    u64* stack = (u64*)runtime->stack;

    stack += ftype->numRets;

    *o_stack = (u8*)stack;

    return m3Err_none;
}


M3Result m3_CallV (IM3Function i_function, ...)
{
    va_list ap;
    va_start(ap, i_function);
    M3Result r = m3_CallVL(i_function, ap);
    va_end(ap);
    return r;
}

static
void ReportNativeStackUsage ()
{
#if d_m3LogNativeStack
    int stackUsed = m3StackGetMax();
    fprintf(stderr, "Native stack used: %d\n", stackUsed);
#endif
}


M3Result m3_CallVL (IM3Function i_function, va_list i_args)
{
    IM3Runtime  runtime = i_function->module->runtime;
    IM3FuncType ftype   = i_function->funcType;
    M3Result    result  = m3Err_none;
    u8*         s       = NULL;

    if (!i_function->compiled) {
        return m3Err_missingCompiledCode;
    }

#if d_m3RecordBacktraces
    ClearBacktrace(runtime);
#endif

    m3StackCheckInit();

_   (checkStartFunction(i_function->module))

_   (GetStackPointerForArgs(i_function, &s))

    for (u32 i = 0; i < ftype->numArgs; ++i) {
        switch (d_FuncArgType(ftype, i)) {
        case c_m3Type_i32:
            *(i32*)(s) = va_arg(i_args, i32);
            s += 8;
            break;
        case c_m3Type_i64:
            *(i64*)(s) = va_arg(i_args, i64);
            s += 8;
            break;
        case c_m3Type_funcref:
        case c_m3Type_externref:
        case c_m3Type_exnref:
            *(uintptr_t*)(s) = va_arg(i_args, uintptr_t);
            s += 8;
            break;
#if d_m3HasFloat
        case c_m3Type_f32:
            *(f32*)(s) = (f32)va_arg(i_args, f64);
            s += 8;
            break; // f32 is passed as f64
        case c_m3Type_f64:
            *(f64*)(s) = va_arg(i_args, f64);
            s += 8;
            break;
#endif
        default: return "unknown argument type";
        }
    }

    result = RunCodeChecked(runtime, i_function);
    ReportNativeStackUsage();

    runtime->lastCalled = result ? NULL : i_function;

    _catch: return result;
}

M3Result m3_Call (IM3Function i_function, uint32_t i_argc, const void* i_argptrs[])
{
    IM3Runtime  runtime = i_function->module->runtime;
    IM3FuncType ftype   = i_function->funcType;
    M3Result    result  = m3Err_none;
    u8*         s       = NULL;

    if (i_argc != ftype->numArgs) {
        return m3Err_argumentCountMismatch;
    }
    if (!i_function->compiled) {
        return m3Err_missingCompiledCode;
    }

#if d_m3RecordBacktraces
    ClearBacktrace(runtime);
#endif

    m3StackCheckInit();

_   (checkStartFunction(i_function->module))

_   (GetStackPointerForArgs(i_function, &s))

    for (u32 i = 0; i < ftype->numArgs; ++i) {
        switch (d_FuncArgType(ftype, i)) {
        case c_m3Type_i32:
            *(i32*)(s) = *(i32*)i_argptrs[i];
            s += 8;
            break;
        case c_m3Type_i64:
            *(i64*)(s) = *(i64*)i_argptrs[i];
            s += 8;
            break;
        case c_m3Type_funcref:
        case c_m3Type_externref:
        case c_m3Type_exnref:
            *(uintptr_t*)(s) = *(uintptr_t*)i_argptrs[i];
            s += 8;
            break;
#if d_m3HasFloat
        case c_m3Type_f32:
            *(f32*)(s) = *(f32*)i_argptrs[i];
            s += 8;
            break;
        case c_m3Type_f64:
            *(f64*)(s) = *(f64*)i_argptrs[i];
            s += 8;
            break;
#endif
        default: return "unknown argument type";
        }
    }

    result = RunCodeChecked(runtime, i_function);

    ReportNativeStackUsage();

    runtime->lastCalled = result ? NULL : i_function;

    _catch: return result;
}

// Argument parsing for m3_CallArgv. Strict on purpose: the whole string has to
// be consumed, so "12abc" is rejected rather than read as 12, and an empty or
// unparsable argument is an error rather than the zero that strtoul with a
// NULL end pointer used to hand back. See wasm3/wasm3#367.
static
M3Result ParseArgInteger (ccstr_t i_arg, u32 i_numBits, u64* o_value)
{
    if (not i_arg or not *i_arg) {
        return "empty argument";
    }

    // strtoull would skip leading space, but trailing space is rejected below;
    // accepting one and not the other would just be confusing
    if (isspace((unsigned char)*i_arg)) {
        return "argument is not a number";
    }

    char* end = NULL;
    u64   value;

    errno = 0;

    // an argument may be spelled signed or unsigned: -1 and 4294967295 name the
    // same i32
    if (*i_arg == '-') {
        i64 signedValue = strtoll(i_arg, &end, 10);

        if (i_numBits == 32 and (signedValue < INT32_MIN or signedValue > INT32_MAX)) {
            return "argument out of range";
        }

        value = (u64)signedValue;
    } else {
        value = strtoull(i_arg, &end, 10);

        if (i_numBits == 32 and value > UINT32_MAX) {
            return "argument out of range";
        }
    }

    if (errno == ERANGE) {
        return "argument out of range";
    }

    if (end == i_arg or *end) {
        return "argument is not a number";
    }

    *o_value = value;

    return m3Err_none;
}


#if d_m3HasFloat
static
M3Result ParseArgFloat (ccstr_t i_arg, f64* o_value)
{
    if (not i_arg or not *i_arg) {
        return "empty argument";
    }

    if (isspace((unsigned char)*i_arg)) {
        return "argument is not a number";
    }

    char* end = NULL;

    errno = 0;

    f64 value = strtod(i_arg, &end);

    if (end == i_arg or *end) {
        return "argument is not a number";
    }

    // strtod reports underflow through ERANGE as well, and a denormal result is
    // perfectly usable, so only an overflow to infinity is out of range
    if (errno == ERANGE and (value > DBL_MAX or value < -DBL_MAX)) {
        return "argument out of range";
    }

    *o_value = value;

    return m3Err_none;
}
#endif


// A reference argument is either the null reference or a host handle written as
// an integer.
static
M3Result ParseArgReference (ccstr_t i_arg, u64* o_value)
{
    if (i_arg and strcmp(i_arg, "null") == 0) {
        *o_value = 0;
        return m3Err_none;
    }

    return ParseArgInteger(i_arg, 64, o_value);
}


M3Result m3_CallArgv (IM3Function i_function, uint32_t i_argc, const char* i_argv[])
{
    IM3FuncType ftype   = i_function->funcType;
    IM3Runtime  runtime = i_function->module->runtime;
    M3Result    result  = m3Err_none;
    u8*         s       = NULL;

    if (i_argc != ftype->numArgs) {
        return m3Err_argumentCountMismatch;
    }
    if (!i_function->compiled) {
        return m3Err_missingCompiledCode;
    }

#if d_m3RecordBacktraces
    ClearBacktrace(runtime);
#endif

    m3StackCheckInit();

_   (checkStartFunction(i_function->module))

_   (GetStackPointerForArgs(i_function, &s))

    for (u32 i = 0; i < ftype->numArgs; ++i) {
        u64 value = 0;
#if d_m3HasFloat
        f64 fvalue = 0;
#endif
        switch (d_FuncArgType(ftype, i)) {
        case c_m3Type_i32:
_           (ParseArgInteger(i_argv[i], 32, &value)) * (i32*)(s) = (i32)value;
            s += 8;
            break;
        case c_m3Type_i64:
_           (ParseArgInteger(i_argv[i], 64, &value)) * (i64*)(s) = (i64)value;
            s += 8;
            break;
        case c_m3Type_funcref:
        case c_m3Type_externref:
        case c_m3Type_exnref:
_           (ParseArgReference(i_argv[i], &value)) * (uintptr_t*)(s) = (uintptr_t)value;
            s += 8;
            break;
#if d_m3HasFloat
            // strtof would be less portable
        case c_m3Type_f32:
_           (ParseArgFloat(i_argv[i], &fvalue)) * (f32*)(s) = (f32)fvalue;
            s += 8;
            break;
        case c_m3Type_f64:
_           (ParseArgFloat(i_argv[i], &fvalue)) * (f64*)(s) = fvalue;
            s += 8;
            break;
#endif
        default: _throw("unknown argument type");
        }
    }

    result = RunCodeChecked(runtime, i_function);

    ReportNativeStackUsage();

    runtime->lastCalled = result ? NULL : i_function;

    _catch: return result;
}


//u8 * AlignStackPointerTo64Bits (const u8 * i_stack)
//{
//    uintptr_t ptr = (uintptr_t) i_stack;
//    return (u8 *) ((ptr + 7) & ~7);
//}


M3Result m3_GetResults (IM3Function i_function, uint32_t i_retc, const void* o_retptrs[])
{
    IM3FuncType ftype   = i_function->funcType;
    IM3Runtime  runtime = i_function->module->runtime;

    if (i_retc != ftype->numRets) {
        return m3Err_argumentCountMismatch;
    }
    if (i_function != runtime->lastCalled) {
        return "function not called";
    }

    u8* s = (u8*)runtime->stack;

    for (u32 i = 0; i < ftype->numRets; ++i) {
        switch (d_FuncRetType(ftype, i)) {
        case c_m3Type_i32:
            *(i32*)o_retptrs[i] = *(i32*)(s);
            s += 8;
            break;
        case c_m3Type_i64:
            *(i64*)o_retptrs[i] = *(i64*)(s);
            s += 8;
            break;
        case c_m3Type_funcref:
        case c_m3Type_externref:
        case c_m3Type_exnref:
            *(uintptr_t*)o_retptrs[i] = *(uintptr_t*)(s);
            s += 8;
            break;
#if d_m3HasFloat
        case c_m3Type_f32:
            *(f32*)o_retptrs[i] = *(f32*)(s);
            s += 8;
            break;
        case c_m3Type_f64:
            *(f64*)o_retptrs[i] = *(f64*)(s);
            s += 8;
            break;
#endif
        default: return "unknown return type";
        }
    }
    return m3Err_none;
}

M3Result m3_GetResultsV (IM3Function i_function, ...)
{
    va_list ap;
    va_start(ap, i_function);
    M3Result r = m3_GetResultsVL(i_function, ap);
    va_end(ap);
    return r;
}

M3Result m3_GetResultsVL (IM3Function i_function, va_list o_rets)
{
    IM3Runtime  runtime = i_function->module->runtime;
    IM3FuncType ftype   = i_function->funcType;

    if (i_function != runtime->lastCalled) {
        return "function not called";
    }

    u8* s = (u8*)runtime->stack;
    for (u32 i = 0; i < ftype->numRets; ++i) {
        switch (d_FuncRetType(ftype, i)) {
        case c_m3Type_i32:
            *va_arg(o_rets, i32*) = *(i32*)(s);
            s += 8;
            break;
        case c_m3Type_i64:
            *va_arg(o_rets, i64*) = *(i64*)(s);
            s += 8;
            break;
        case c_m3Type_funcref:
        case c_m3Type_externref:
        case c_m3Type_exnref:
            *va_arg(o_rets, uintptr_t*) = *(uintptr_t*)(s);
            s += 8;
            break;
#if d_m3HasFloat
        case c_m3Type_f32:
            *va_arg(o_rets, f32*) = *(f32*)(s);
            s += 8;
            break;
        case c_m3Type_f64:
            *va_arg(o_rets, f64*) = *(f64*)(s);
            s += 8;
            break;
#endif
        default: return "unknown argument type";
        }
    }
    return m3Err_none;
}

void ReleaseCodePageNoTrack (IM3Runtime i_runtime, IM3CodePage i_codePage)
{
    if (i_codePage) {
        IM3CodePage* list;

        bool pageFull = (NumFreeLines(i_codePage) < d_m3CodePageFreeLinesThreshold);
        if (pageFull) {
            list = &i_runtime->pagesFull;
        } else {
            list = &i_runtime->pagesOpen;
        }

        PushCodePage(list, i_codePage);                         m3log (emit, "release page: %d to queue: '%s'", i_codePage->info.sequence, pageFull ? "full" : "open")
    }
}


IM3CodePage AcquireCodePageWithCapacity (IM3Runtime i_runtime, u32 i_minLineCount)
{
    IM3CodePage page = RemoveCodePageOfCapacity(&i_runtime->pagesOpen, i_minLineCount);

    if (not page) {
        page = Environment_AcquireCodePage(i_runtime->environment, i_minLineCount);

        if (not page) {
            page = NewCodePage(i_runtime, i_minLineCount);
        }

        if (page) {
            i_runtime->numCodePages++;
        }
    }

    if (page) {                                                  m3log (emit, "acquire page: %d", page->info.sequence);
        i_runtime->numActiveCodePages++;
    }

    return page;
}


IM3CodePage AcquireCodePage (IM3Runtime i_runtime)
{
    return AcquireCodePageWithCapacity(i_runtime, d_m3CodePageFreeLinesThreshold);
}


void ReleaseCodePage (IM3Runtime i_runtime, IM3CodePage i_codePage)
{
    if (i_codePage) {
        ReleaseCodePageNoTrack(i_runtime, i_codePage);
        i_runtime->numActiveCodePages--;

#if defined(DEBUG)
        u32 numOpen = CountCodePages(i_runtime->pagesOpen);
        u32 numFull = CountCodePages(i_runtime->pagesFull);

        m3log(runtime, "runtime: %p; open-pages: %d; full-pages: %d; active: %d; total: %d", i_runtime, numOpen, numFull, i_runtime->numActiveCodePages, i_runtime->numCodePages);

        d_m3Assert(numOpen + numFull + i_runtime->numActiveCodePages == i_runtime->numCodePages);

#  if d_m3LogCodePages
        dump_code_page(i_codePage, /* startPC: */ NULL);
#  endif
#endif
    }
}


#if d_m3VerboseErrorMessages
M3Result m3Error (M3Result i_result, IM3Runtime i_runtime, IM3Module i_module, IM3Function i_function,
                  const char* const i_file, u32 i_lineNum, const char* const i_errorMessage, ...)
{
    if (i_runtime) {
        i_runtime->error         = (M3ErrorInfo){ .result = i_result, .runtime = i_runtime, .module = i_module, .function = i_function, .file = i_file, .line = i_lineNum };
        i_runtime->error.message = i_runtime->error_message;

        va_list args;
        va_start(args, i_errorMessage);
        vsnprintf(i_runtime->error_message, sizeof(i_runtime->error_message), i_errorMessage, args);
        va_end(args);
    }

    return i_result;
}
#endif


void m3_GetErrorInfo (IM3Runtime i_runtime, M3ErrorInfo* o_info)
{
    if (i_runtime) {
        *o_info = i_runtime->error;
        m3_ResetErrorInfo(i_runtime);
    }
}


void m3_ResetErrorInfo (IM3Runtime i_runtime)
{
    if (i_runtime) {
        M3_INIT(i_runtime->error);
        i_runtime->error.message = "";
    }
}

uint8_t* m3_GetMemory (IM3Module i_module, size_t* o_memorySizeInBytes, uint32_t i_memoryIndex)
{
    uint8_t* memory = NULL;
    size_t   size   = 0;

    if (i_module and i_memoryIndex < i_module->numMemories) {
        IM3Memory mem = i_module->memories[i_memoryIndex];

        if (mem->mallocated) {
            size = mem->mallocated->length;

            if (size) {
                memory = m3MemData(mem->mallocated);
            }
        }
    }

    if (o_memorySizeInBytes) {
        *o_memorySizeInBytes = size;
    }

    return memory;
}


size_t m3_GetMemorySize (IM3Module i_module, uint32_t i_memoryIndex)
{
    if (not i_module or i_memoryIndex >= i_module->numMemories) {
        return 0;
    }

    IM3Memory mem = i_module->memories[i_memoryIndex];

    return mem->mallocated ? mem->mallocated->length : 0;
}


size_t m3_GetMemorySizeAt (const void* i_memory)
{
    if (not i_memory) {
        return 0;
    }

    // the header sits immediately before the data it describes
    const M3MemoryHeader* header = ((const M3MemoryHeader*)i_memory) - 1;

    return header->length;
}


M3Result m3_FindExportedMemory (IM3Module i_module, cstr_t i_name, u32* o_memoryIndex)
{
    if (not i_module or not i_name or not o_memoryIndex) {
        return m3Err_unknownMemory;
    }

    for (u32 i = 0; i < i_module->numMemories; ++i) {
        IM3Memory memory = i_module->memories[i];

        if (memory->exportName and strcmp(memory->exportName, i_name) == 0) {
            *o_memoryIndex = i;
            return m3Err_none;
        }
    }

    return m3Err_unknownMemory;
}


bool Module_HasLinkedHostImport (IM3Module i_module, cstr_t i_importModule)
{
    for (u32 i = 0; i < i_module->numFunctions; ++i) {
        IM3Function f = &i_module->functions[i];

        // hostMemory is set when a host function is bound to the import, so it
        // is also the mark of one - see CompileRawFunction
        if (not f->hostMemory or f->wasm or f->resolved) {
            continue;
        }

        if (f->import.moduleUtf8 and strcmp(f->import.moduleUtf8, i_importModule) == 0) {
            return true;
        }
    }

    return false;
}


M3Result m3_BindImportMemory (IM3Module io_module, cstr_t i_importModule, u32 i_memoryIndex)
{
    if (not io_module or not i_importModule) {
        return m3Err_moduleNotLinked;
    }

    if (i_memoryIndex >= io_module->numMemories) {
        return m3Err_unknownMemory;
    }

    // the memory itself, not the index: linking may have pointed this slot at
    // another module's memory, and growing one reallocates behind it
    IM3Memory memory = io_module->memories[i_memoryIndex];

    const bool wildcardModule = (strcmp(i_importModule, "*") == 0);

    for (u32 i = 0; i < io_module->numFunctions; ++i) {
        IM3Function f = &io_module->functions[i];

        // only an import that a host function was bound to has a memory to
        // pin: one with a body of its own reads memory through the interpreter,
        // and one resolved to another module's export runs that module's code
        if (not f->hostMemory or f->wasm or f->resolved) {
            continue;
        }

        if (wildcardModule or (f->import.moduleUtf8 and strcmp(f->import.moduleUtf8, i_importModule) == 0)) {
            f->hostMemory = memory;
        }
    }

    return m3Err_none;
}


M3BacktraceInfo* m3_GetBacktrace (IM3Runtime i_runtime)
{
#if d_m3RecordBacktraces
    return &i_runtime->backtrace;
#else
    return NULL;
#endif
}


//-------------------------------------------------------------------------------------------------------------------------------
//  snapshots & suspendable execution
//-------------------------------------------------------------------------------------------------------------------------------

void m3_SetSuspendable (IM3Runtime io_runtime, bool i_suspendable)
{
    if (io_runtime) {
        io_runtime->isSuspendable = i_suspendable;
    }
}

void m3_RequestSuspend (IM3Runtime io_runtime)
{
    if (io_runtime) {
        io_runtime->suspendRequested = true;
    }
}

bool m3_IsSuspended (IM3Runtime i_runtime)
{
    if (!i_runtime) {
        return false;
    }
#if d_m3HasStackSwitching
    if (i_runtime->rootContinuation && i_runtime->rootContinuation->state == cont_suspended) {
        return true;
    }
#endif
    return false;
}

#define d_m3ChunkEnd      0x00
#define d_m3ChunkRaw      0x01
#define d_m3ChunkFillFF   0x02

#if d_m3HasSnapshots

static const u8 s_snapshotHeader[] = { 'W', '3', 'S', 1 };

#  define d_m3SnapshotFlagPostmortem  0x1

static
M3Result StreamWriteMemoryChunks (const u8* i_bytes, size_t i_size, M3SnapshotWriter i_writer, void* i_userdata)
{
    M3Result result = m3Err_none;
    size_t   cursor = 0;

    while (cursor < i_size) {
        u8 byte = i_bytes[cursor];
        if (byte == 0x00 || byte == 0xFF) {
            size_t run = 1;
            while (cursor + run < i_size && i_bytes[cursor + run] == byte) {
                run++;
            }
            if (run >= d_m3SnapshotRunThreshold) {
                if (byte == 0xFF) {
                    u8  type = d_m3ChunkFillFF;
                    u32 off  = (u32)cursor;
                    u32 len  = (u32)run;
_                   (i_writer(&type, sizeof(type), i_userdata));
_                   (i_writer(&off, sizeof(off), i_userdata));
_                   (i_writer(&len, sizeof(len), i_userdata));
                }
                cursor += run;
                continue;
            }
        }

        {
            size_t rawStart = cursor;
            while (cursor < i_size) {
                u8 b = i_bytes[cursor];
                if (b == 0x00 || b == 0xFF) {
                    size_t run = 1;
                    while (cursor + run < i_size && i_bytes[cursor + run] == b) {
                        run++;
                    }
                    if (run >= d_m3SnapshotRunThreshold) {
                        break;
                    }
                    cursor += run;
                } else {
                    cursor++;
                }
            }

            size_t rawLen = cursor - rawStart;
            if (rawLen > 0) {
                u8  type = d_m3ChunkRaw;
                u32 off  = (u32)rawStart;
                u32 len  = (u32)rawLen;
_               (i_writer(&type, sizeof(type), i_userdata));
_               (i_writer(&off, sizeof(off), i_userdata));
_               (i_writer(&len, sizeof(len), i_userdata));
_               (i_writer(i_bytes + rawStart, len, i_userdata));
            }
        }
    }

    {
        u8 endType = d_m3ChunkEnd;
_       (i_writer(&endType, sizeof(endType), i_userdata));
    }

_catch:
    return result;
}

static
M3Result StreamReadMemoryChunks (u8* o_bytes, size_t i_size, M3SnapshotReader i_reader, void* i_userdata)
{
    M3Result result = m3Err_none;

    for (;;) {
        u8 chunkType = 0;
_       (i_reader(&chunkType, sizeof(chunkType), i_userdata));
        if (chunkType == d_m3ChunkEnd) {
            break;
        } else if (chunkType == d_m3ChunkRaw) {
            u32 off = 0, len = 0;
_           (i_reader(&off, sizeof(off), i_userdata));
_           (i_reader(&len, sizeof(len), i_userdata));
            if ((size_t)off + len > i_size) {
                _throw(m3Err_trapOutOfBoundsMemoryAccess);
            }
_           (i_reader(o_bytes + off, len, i_userdata));
        } else if (chunkType == d_m3ChunkFillFF) {
            u32 off = 0, len = 0;
_           (i_reader(&off, sizeof(off), i_userdata));
_           (i_reader(&len, sizeof(len), i_userdata));
            if ((size_t)off + len > i_size) {
                _throw(m3Err_trapOutOfBoundsMemoryAccess);
            }
            memset(o_bytes + off, 0xFF, len);
        } else {
            _throw(m3Err_wasmMalformed);
        }
    }

_catch:
    return result;
}

#endif // d_m3HasSnapshots

M3Result m3_SaveSnapshot (IM3Runtime io_runtime, M3SnapshotWriter i_writer, void* i_userdata)
{
    M3Result result = m3Err_none;
    _throwifnull(io_runtime);
    _throwifnull(i_writer);

#if d_m3HasSnapshots
    {
        // Anything but a paused invocation is saved as a postmortem: the state
        // of the module the last call entered, which needs no continuation and
        // so no suspendable runtime.
        IM3Continuation cont       = io_runtime->rootContinuation;
        bool            postmortem = not cont or cont->state != cont_suspended;

        IM3Function entry = postmortem ? io_runtime->entered : cont->entryFunction;
        if (!entry) {
            return m3Err_none;
        }
        IM3Module module = entry->module;

        // A capture that reaches through a resume carries a second continuation
        // with a stack and frames of its own, which this format has no room
        // for. Refusing is the only honest answer: writing the frame without
        // what it points at produces a file that crashes on load.
        if (!postmortem) {
            for (u32 i = 0; i < cont->numFrames; ++i) {
                if (cont->frames[i].kind == frame_resume) {
                    _throw("cannot snapshot a continuation captured across a resume");
                }
            }
        }

        u32 flags = postmortem ? d_m3SnapshotFlagPostmortem : 0;
_       (i_writer(s_snapshotHeader, sizeof(s_snapshotHeader), i_userdata));
_       (i_writer(&flags, sizeof(flags), i_userdata));

        // Linear memory
        u32 numMemories = module->numMemories;
_       (i_writer(&numMemories, sizeof(numMemories), i_userdata));
        for (u32 m = 0; m < numMemories; ++m) {
            IM3Memory memory   = module->memories ? module->memories[m] : NULL;
            u64       numPages = memory ? memory->numPages : 0;
            u64       maxPages = memory ? memory->maxPages : 0;
            u32       pageSize = memory ? Memory_PageSize(memory) : d_m3DefaultMemPageSize;
_           (i_writer(&numPages, sizeof(numPages), i_userdata));
_           (i_writer(&maxPages, sizeof(maxPages), i_userdata));
_           (i_writer(&pageSize, sizeof(pageSize), i_userdata));
            if (memory && memory->mallocated) {
                size_t bytes    = (size_t)numPages * pageSize;
                u8*    memBytes = m3MemData(memory->mallocated);
_               (StreamWriteMemoryChunks(memBytes, bytes, i_writer, i_userdata));
            }
        }

        // Globals
        u32 numGlobals = module->numGlobals;
_       (i_writer(&numGlobals, sizeof(numGlobals), i_userdata));
        for (u32 g = 0; g < numGlobals; ++g) {
            M3Global* global = &module->globals[g];
            u8        type   = (u8)global->type;
            u64       val    = 0;
            if (Is64BitType(global->type)) {
                val = global->i64Value;
            } else {
                val = (u64)global->i32Value;
            }
_           (i_writer(&type, sizeof(type), i_userdata));
_           (i_writer(&val, sizeof(val), i_userdata));
        }

        // Tables
        u32 numTables = module->numTables;
_       (i_writer(&numTables, sizeof(numTables), i_userdata));
        for (u32 t = 0; t < numTables; ++t) {
            IM3Table table = module->tables ? module->tables[t] : NULL;
            u32      size  = table ? table->size : 0;
_           (i_writer(&size, sizeof(size), i_userdata));
            for (u32 e = 0; e < size; ++e) {
                void* elem      = table ? table->elements[e] : NULL;
                i32   funcIndex = -1;
                if (elem && table && BaseTypeOf(table->type) == c_m3Type_funcref) {
                    IM3Function func = (IM3Function)elem;
                    funcIndex        = (i32)(func - func->module->functions);
                }
_               (i_writer(&funcIndex, sizeof(funcIndex), i_userdata));
            }
        }

        // the function the suspension point is in
        IM3Function currentFunc = postmortem ? NULL : Continuation_InnermostFunction(cont);

        // Value Stack. Everything below sp belongs to the frames that are
        // waiting, and everything above it up to what the innermost function
        // asked for is that function's locals and working space - saving less
        // than that brings the program back with its locals full of nothing.
        u32 spSlot         = 0;
        u32 numSlotsToSave = 0;
        if (!postmortem) {
            spSlot         = (u32)((m3slot_t*)cont->sp - (m3slot_t*)io_runtime->originStack);
            numSlotsToSave = spSlot + (currentFunc ? currentFunc->maxStackSlots : 0);

            if (numSlotsToSave > io_runtime->numStackSlots) {
                numSlotsToSave = io_runtime->numStackSlots;
            }
        }
_       (i_writer(&spSlot, sizeof(spSlot), i_userdata));
_       (i_writer(&numSlotsToSave, sizeof(numSlotsToSave), i_userdata));
_       (i_writer(io_runtime->originStack, numSlotsToSave * sizeof(m3slot_t), i_userdata));

        // Continuation & execution state

        u32 entryFuncIndex   = postmortem ? 0 : (u32)(cont->entryFunction - module->functions);
        u32 currentFuncIndex = postmortem ? 0 : (u32)(currentFunc - module->functions);
        u32 pcOffset         = postmortem ? 0 : (u32)(cont->pc - currentFunc->compiled);
        u64 r0               = postmortem ? 0 : (u64)cont->r0;
        f64 fp0              = 0.;
#  if d_m3HasFloat
        if (!postmortem) {
            fp0 = cont->fp0;
        }
#  endif
        u32 numFrames = postmortem ? 0 : cont->numFrames;

_       (i_writer(&entryFuncIndex, sizeof(entryFuncIndex), i_userdata));
_       (i_writer(&currentFuncIndex, sizeof(currentFuncIndex), i_userdata));
_       (i_writer(&pcOffset, sizeof(pcOffset), i_userdata));
_       (i_writer(&r0, sizeof(r0), i_userdata));
_       (i_writer(&fp0, sizeof(fp0), i_userdata));
_       (i_writer(&numFrames, sizeof(numFrames), i_userdata));

        IM3Function frameFunc = entry;
        for (i32 i = (i32)numFrames - 1; i >= 0; --i) {
            M3Frame* f          = &cont->frames[i];
            u8       kind       = f->kind;
            u32      fSpSlot    = (u32)((m3slot_t*)f->sp - (m3slot_t*)io_runtime->originStack);
            u32      fFuncIndex = (u32)(frameFunc - module->functions);
            u32      fPcOffset  = (u32)(f->pc - frameFunc->compiled);
            u64      fr0        = (kind == frame_call) ? (u64)f->call.r0 : 0;
            f64      ffp0       = 0.;
#  if d_m3HasFloat
            if (kind == frame_call) {
                ffp0 = f->call.fp0;
            }
#  endif
            u32 calleeIndex = 0;
            if (kind == frame_call && f->call.function) {
                calleeIndex = (u32)(f->call.function - module->functions);
            }
            u32 numClauses   = 0;
            u8  handlersLive = 0;
#  if d_m3HasExceptionHandling
            if (kind == frame_try) {
                numClauses   = f->try_.numClauses;
                handlersLive = (u8)f->try_.handlersLive;
            }
#  endif

_           (i_writer(&kind, sizeof(kind), i_userdata));
_           (i_writer(&fSpSlot, sizeof(fSpSlot), i_userdata));
_           (i_writer(&fFuncIndex, sizeof(fFuncIndex), i_userdata));
_           (i_writer(&fPcOffset, sizeof(fPcOffset), i_userdata));
_           (i_writer(&fr0, sizeof(fr0), i_userdata));
_           (i_writer(&ffp0, sizeof(ffp0), i_userdata));
_           (i_writer(&calleeIndex, sizeof(calleeIndex), i_userdata));
_           (i_writer(&numClauses, sizeof(numClauses), i_userdata));
_           (i_writer(&handlersLive, sizeof(handlersLive), i_userdata));

            if (kind == frame_call && f->call.function) {
                frameFunc = f->call.function;
            }
        }
    }
#endif

_catch:
    return result;
}

M3Result m3_LoadSnapshot (IM3Runtime io_runtime, IM3Module i_module, M3SnapshotReader i_reader, void* i_userdata)
{
    M3Result result = m3Err_none;
    _throwifnull(io_runtime);
    _throwifnull(i_module);
    _throwifnull(i_reader);

#if d_m3HasSnapshots
    {
        u8  header[sizeof(s_snapshotHeader)] = { 0 };
        u32 flags                            = 0;
_       (i_reader(header, sizeof(header), i_userdata));
_       (i_reader(&flags, sizeof(flags), i_userdata));
        if (memcmp(header, s_snapshotHeader, sizeof(header)) != 0) {
            _throw(m3Err_wasmMalformed);
        }
        if (flags & d_m3SnapshotFlagPostmortem) {
            _throw("postmortem snapshots cannot be resumed");
        }

        // Before anything here compiles a function: what comes out of a
        // snapshot has to be able to suspend again, and a body compiled while
        // this is still false gets back edges that never will.
        io_runtime->isSuspendable = true;

        // Linear Memory
        u32 numMemories = 0;
_       (i_reader(&numMemories, sizeof(numMemories), i_userdata));
        if (numMemories > i_module->numMemories) {
            _throw(m3Err_wasmMalformed);
        }
        for (u32 m = 0; m < numMemories; ++m) {
            u64 numPages = 0, maxPages = 0;
            u32 pageSize = 0;
_           (i_reader(&numPages, sizeof(numPages), i_userdata));
_           (i_reader(&maxPages, sizeof(maxPages), i_userdata));
_           (i_reader(&pageSize, sizeof(pageSize), i_userdata));
            IM3Memory memory = i_module->memories ? i_module->memories[m] : NULL;
            if (memory) {
                _throwif(m3Err_wasmMalformed, pageSize != Memory_PageSize(memory));
                if (memory->numPages < numPages) {
_                   (ResizeMemory(io_runtime, memory, numPages));
                }
                size_t bytes    = (size_t)numPages * pageSize;
                u8*    memBytes = m3MemData(memory->mallocated);
                memset(memBytes, 0, bytes);
_               (StreamReadMemoryChunks(memBytes, bytes, i_reader, i_userdata));
            }
        }

        // Globals
        u32 numGlobals = 0;
_       (i_reader(&numGlobals, sizeof(numGlobals), i_userdata));
        for (u32 g = 0; g < numGlobals; ++g) {
            u8  type = 0;
            u64 val  = 0;
_           (i_reader(&type, sizeof(type), i_userdata));
_           (i_reader(&val, sizeof(val), i_userdata));
            if (g < i_module->numGlobals) {
                M3Global* global = &i_module->globals[g];
                if (Is64BitType(global->type)) {
                    global->i64Value = val;
                } else {
                    global->i32Value = (u32)val;
                }
            }
        }

        // Tables
        u32 numTables = 0;
_       (i_reader(&numTables, sizeof(numTables), i_userdata));
        if (numTables > i_module->numTables) {
            _throw(m3Err_wasmMalformed);
        }
        for (u32 t = 0; t < numTables; ++t) {
            u32 size = 0;
_           (i_reader(&size, sizeof(size), i_userdata));
            IM3Table table = i_module->tables ? i_module->tables[t] : NULL;
            for (u32 e = 0; e < size; ++e) {
                i32 funcIndex = -1;
_               (i_reader(&funcIndex, sizeof(funcIndex), i_userdata));
                if (table && e < table->size) {
                    if (funcIndex >= 0 && (u32)funcIndex < i_module->numFunctions) {
                        table->elements[e] = &i_module->functions[funcIndex];
                    } else {
                        table->elements[e] = NULL;
                    }
                }
            }
        }

        // Value Stack
        u32 spSlot = 0, numSavedSlots = 0;
_       (i_reader(&spSlot, sizeof(spSlot), i_userdata));
_       (i_reader(&numSavedSlots, sizeof(numSavedSlots), i_userdata));
        if (numSavedSlots > io_runtime->numStackSlots) {
            _throw(m3Err_trapStackOverflow);
        }
        // the frame the suspension point is in starts here, so it has to be
        // inside what the file actually carried
        if (spSlot > numSavedSlots) {
            _throw(m3Err_wasmMalformed);
        }
_       (i_reader(io_runtime->originStack, numSavedSlots * sizeof(m3slot_t), i_userdata));

        // Continuation
        u32 entryFuncIndex = 0, currentFuncIndex = 0, pcOffset = 0;
        u64 r0        = 0;
        f64 fp0       = 0.;
        u32 numFrames = 0;
_       (i_reader(&entryFuncIndex, sizeof(entryFuncIndex), i_userdata));
_       (i_reader(&currentFuncIndex, sizeof(currentFuncIndex), i_userdata));
_       (i_reader(&pcOffset, sizeof(pcOffset), i_userdata));
_       (i_reader(&r0, sizeof(r0), i_userdata));
_       (i_reader(&fp0, sizeof(fp0), i_userdata));
_       (i_reader(&numFrames, sizeof(numFrames), i_userdata));

        if (entryFuncIndex >= i_module->numFunctions || currentFuncIndex >= i_module->numFunctions) {
            _throw(m3Err_wasmMalformed);
        }
        if (numFrames > d_m3ContinuationMaxFrames) {
            _throw(m3Err_wasmMalformed);
        }

        IM3Function entryFunc = &i_module->functions[entryFuncIndex];
        if (!entryFunc->compiled) {
_           (CompileFunction(entryFunc));
        }
        IM3Function currentFunc = &i_module->functions[currentFuncIndex];
        if (!currentFunc->compiled) {
_           (CompileFunction(currentFunc));
        }

        if (!io_runtime->rootContinuation) {
            io_runtime->rootContinuation = Continuation_New(io_runtime, entryFunc->funcType, entryFunc);
            if (io_runtime->rootContinuation && io_runtime->rootContinuation->valStack) {
                m3_Free(io_runtime->rootContinuation->valStack);
                io_runtime->rootContinuation->valStack = NULL;
            }
            if (io_runtime->rootContinuation) {
                io_runtime->rootContinuation->numStackSlots = io_runtime->numStackSlots;
            }
        }
        IM3Continuation cont = io_runtime->rootContinuation;
        _throwifnull(cont);

        cont->entryFunction = entryFunc;
        cont->state         = cont_suspended;
        cont->sp            = (m3stack_t)io_runtime->originStack + spSlot;
        cont->pc            = currentFunc->compiled + pcOffset;
        cont->r0            = (m3reg_t)r0;
#  if d_m3HasFloat
        cont->fp0 = fp0;
#  endif

        if (numFrames > cont->framesCap) {
            m3_Free(cont->frames);
            cont->frames = m3_AllocArray(M3Frame, numFrames);
            _throwifnull(cont->frames);
            cont->framesCap = numFrames;
        }
        cont->numFrames = numFrames;

        for (i32 i = (i32)numFrames - 1; i >= 0; --i) {
            u8  kind    = 0;
            u32 fSpSlot = 0, fFuncIndex = 0, fPcOffset = 0;
            u64 fr0         = 0;
            f64 ffp0        = 0.;
            u32 calleeIndex = 0, numClauses = 0;
            u8  handlersLive = 0;

_           (i_reader(&kind, sizeof(kind), i_userdata));
_           (i_reader(&fSpSlot, sizeof(fSpSlot), i_userdata));
_           (i_reader(&fFuncIndex, sizeof(fFuncIndex), i_userdata));
_           (i_reader(&fPcOffset, sizeof(fPcOffset), i_userdata));
_           (i_reader(&fr0, sizeof(fr0), i_userdata));
_           (i_reader(&ffp0, sizeof(ffp0), i_userdata));
_           (i_reader(&calleeIndex, sizeof(calleeIndex), i_userdata));
_           (i_reader(&numClauses, sizeof(numClauses), i_userdata));
_           (i_reader(&handlersLive, sizeof(handlersLive), i_userdata));

            if (fFuncIndex >= i_module->numFunctions) {
                _throw(m3Err_wasmMalformed);
            }
            // frame_resume names a whole second continuation, which the writer
            // refuses to produce, so anything claiming to be one here is a file
            // that cannot be trusted rather than a frame that can be rebuilt
            bool knownKind = (kind == frame_call or kind == frame_loop);
#  if d_m3HasExceptionHandling
            knownKind = knownKind or (kind == frame_try);
#  endif
#  if d_m3EntryKeepsFrame
            knownKind = knownKind or (kind == frame_entry);
#  endif
            if (not knownKind or fSpSlot > io_runtime->numStackSlots) {
                _throw(m3Err_wasmMalformed);
            }
            IM3Function fFunc = &i_module->functions[fFuncIndex];
            if (!fFunc->compiled) {
_               (CompileFunction(fFunc));
            }

            M3Frame* f = &cont->frames[i];
            f->kind    = kind;
            f->sp      = (m3stack_t)io_runtime->originStack + fSpSlot;
            f->pc      = fFunc->compiled + fPcOffset;
            f->memory  = Module_Memory0(i_module);

            if (kind == frame_call) {
                if (calleeIndex < i_module->numFunctions) {
                    IM3Function calleeFunc = &i_module->functions[calleeIndex];
                    if (!calleeFunc->compiled) {
_                       (CompileFunction(calleeFunc));
                    }
                    f->call.function = calleeFunc;
                } else {
                    f->call.function = NULL;
                }
                f->call.r0 = (m3reg_t)fr0;
#  if d_m3HasFloat
                f->call.fp0 = ffp0;
#  endif
            }
#  if d_m3HasExceptionHandling
            else if (kind == frame_try) {
                f->try_.numClauses   = numClauses;
                f->try_.handlersLive = (bool)handlersLive;
            }
#  endif
        }

        // A suspension is only ever reached with the start function run, or
        // under way in the invocation being restored, so the memories and
        // globals above already hold what it did. Running it again on top of
        // them would do it twice.
        i_module->startFunction = -1;

        Runtime_PlaceCallStack(io_runtime);
    }
#endif

_catch:
    return result;
}

typedef struct M3BufferWriter {
    u8*    buffer;
    size_t size;
    size_t capacity;
} M3BufferWriter;

static
M3Result BufferWriter_Write (const void* i_data, size_t i_size, void* i_userdata)
{
    M3BufferWriter* bw = (M3BufferWriter*)i_userdata;
    if (bw->size + i_size > bw->capacity) {
        size_t newCap = bw->capacity ? bw->capacity * 2 : 4096;
        while (bw->size + i_size > newCap) {
            newCap *= 2;
        }
        u8* newBuf = m3_ReallocArray(u8, bw->buffer, newCap, bw->capacity);
        if (!newBuf) {
            return m3Err_mallocFailed;
        }
        bw->buffer   = newBuf;
        bw->capacity = newCap;
    }
    memcpy(bw->buffer + bw->size, i_data, i_size);
    bw->size += i_size;
    return m3Err_none;
}

typedef struct M3BufferReader {
    const u8* buffer;
    size_t    size;
    size_t    cursor;
} M3BufferReader;

static
M3Result BufferReader_Read (void* o_buffer, size_t i_size, void* i_userdata)
{
    M3BufferReader* br = (M3BufferReader*)i_userdata;
    if (br->cursor + i_size > br->size) {
        return m3Err_wasmMalformed;
    }
    memcpy(o_buffer, br->buffer + br->cursor, i_size);
    br->cursor += i_size;
    return m3Err_none;
}

M3Result m3_SaveSnapshotToBuffer (IM3Runtime io_runtime, void** o_bytes, size_t* o_size)
{
    if (!io_runtime || !o_bytes || !o_size) {
        return m3Err_mallocFailed;
    }
    M3BufferWriter bw     = { NULL, 0, 0 };
    M3Result       result = m3_SaveSnapshot(io_runtime, BufferWriter_Write, &bw);
    if (result) {
        m3_Free(bw.buffer);
        *o_bytes = NULL;
        *o_size  = 0;
        return result;
    }
    *o_bytes = bw.buffer;
    *o_size  = bw.size;
    return m3Err_none;
}

M3Result m3_LoadSnapshotFromBuffer (IM3Runtime io_runtime, IM3Module i_module, const void* i_bytes, size_t i_size)
{
    if (!io_runtime || !i_module || !i_bytes) {
        return m3Err_mallocFailed;
    }
    M3BufferReader br = { (const u8*)i_bytes, i_size, 0 };
    return m3_LoadSnapshot(io_runtime, i_module, BufferReader_Read, &br);
}

M3Result m3_ResumeRuntime (IM3Runtime io_runtime)
{
    if (!io_runtime) {
        return m3Err_mallocFailed;
    }
    if (!io_runtime->isSuspendable) {
        return m3Err_none;
    }
#if d_m3HasStackSwitching
    if (!io_runtime->rootContinuation || io_runtime->rootContinuation->state != cont_suspended) {
        return m3Err_none;
    }
    // what finishes returns into the bottom of the stack, where it was entered
    io_runtime->stack = io_runtime->originStack;

    // this is the outermost call again, as far as a host function calling back
    // into Wasm from inside it can tell
    io_runtime->entered = io_runtime->rootContinuation->entryFunction;
    io_runtime->callNesting++;

    d_m3StackLimitEnter(io_runtime);
    M3Result result = (M3Result)ResumeContinuation(io_runtime, io_runtime->rootContinuation);
    d_m3StackLimitLeave(io_runtime);

    io_runtime->callNesting--;

    Runtime_PlaceCallStack(io_runtime);
    return result;
#else
    return m3Err_none;
#endif
}
