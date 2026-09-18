//
//  m3_snapshot.c
//
//  Copyright © 2026 Volodymyr Shymanskyy. All rights reserved.
//
//  Saving a runtime's execution state to a stream and bringing it back.
//
//  A snapshot is written in this build's own terms - slot width, byte order,
//  metacode - and the header says which, so a mismatch is refused rather than
//  resumed into. What it never writes is an address. A pc becomes a function
//  and a count of that function's metacode words; a reference becomes a
//  function index or the id of a continuation or exception the snapshot carries
//  whole. The compiler's snapshot maps (see M3SnapshotMap) are what make both
//  possible: the value stack is untyped, and they say which slots hold what.
//

#include "m3_env.h"
#include "m3_compile.h"
#include "m3_exception.h"

#include <limits.h>

#if d_m3HasSnapshots

//---------------------------------------------------------------------------------------------------------------------------------
//  format
//---------------------------------------------------------------------------------------------------------------------------------

static const u8 c_snapshotMagic[4] = { 'W', '3', 'S', 1 };

#  define d_m3SnapshotByteOrder       0x0102

#  define d_m3SnapshotFlagPostmortem  0x1

// no index, and no reference
#  define d_m3SnapshotNone            UINT32_MAX
#  define d_m3SnapshotNullRef         UINT64_MAX

// A continuation's state, as the file spells it. A running continuation can
// only be written into a postmortem, where it is as finished as the rest.
enum {
    snapshot_contAllocated = 0,
    snapshot_contSuspended = 1,
    snapshot_contFinished  = 2
};

// Frame kinds, as the file spells them: M3FrameKind is not stable across
// builds, since some of its members exist only with some features.
enum {
    snapshot_frameCall   = 0,
    snapshot_frameLoop   = 1,
    snapshot_frameTry    = 2,
    snapshot_frameEntry  = 3,
    snapshot_frameResume = 4
};

// Memory contents go out as runs, so the long stretches of zero or 0xFF that
// most memories are made of cost a few bytes rather than their length
#  define d_m3ChunkEnd                0x00
#  define d_m3ChunkRaw                0x01
#  define d_m3ChunkFillFF             0x02


//---------------------------------------------------------------------------------------------------------------------------------
//  identity
//---------------------------------------------------------------------------------------------------------------------------------

static
u64 HashBytes (u64 i_hash, const void* i_bytes, size_t i_size)
{
    const u8* bytes = (const u8*)i_bytes;

    for (size_t i = 0; i < i_size; ++i) {
        i_hash = (i_hash ^ bytes[i]) * 0x100000001b3ULL;
    }

    return i_hash;
}

#  define d_m3HashSeed                0xcbf29ce484222325ULL

// Whatever changes the metacode a body compiles to, which is what every pc in
// a snapshot is counted in. Two builds that agree on all of this emit the same
// words for the same Wasm.
static
u64 BuildFingerprint (void)
{
    static const u32 c_config[] = {
        (u32)sizeof(void*),
        (u32)sizeof(m3slot_t),
        d_m3HasFloat,
        d_m3FoldSetLocal,
        d_m3FuseBranch,
        d_m3EntryKeepsFrame,
        d_m3CanTailCall,
        d_m3HasExceptionHandling,
        d_m3HasStackSwitching,
        d_m3HasTypedRefs,
        d_m3HasMultiMemory,
        d_m3HasMemory64,
        d_m3GuardedMemory,
        d_m3SkipMemoryBoundsCheck,
        d_m3HasGasMetering,
        d_m3EnableOpTracing,
        d_m3EnableOpProfiling,
        d_m3EnableStrace,
        d_m3MaxContinuationPayload,
        d_m3ContinuationStackSlots,
        d_m3ContinuationMaxFrames,
    };

    u64 hash = HashBytes(d_m3HashSeed, M3_VERSION, sizeof(M3_VERSION));

    return HashBytes(hash, c_config, sizeof(c_config));
}

static
u64 ModuleFingerprint (IM3Module i_module)
{
    size_t size = (i_module->wasmStart and i_module->wasmEnd > i_module->wasmStart)
                    ? (size_t)(i_module->wasmEnd - i_module->wasmStart)
                    : 0;

    return HashBytes(d_m3HashSeed, i_module->wasmStart, size);
}

static
bool IsGasMetered (IM3Runtime i_runtime)
{
#  if d_m3HasGasMetering
    return i_runtime->gasLimit != 0;
#  else
    (void)i_runtime;
    return false;
#  endif
}


//---------------------------------------------------------------------------------------------------------------------------------
//  pc <-> (function, offset)
//---------------------------------------------------------------------------------------------------------------------------------

// A pc on a run's end names the word that follows the run - a bridge, which
// carries on to wherever the function continues - so a pc that starts one run
// and ends another belongs to the run it starts.
static
M3Result PcToOffset (IM3Function i_function, pc_t i_pc, u32* o_offset)
{
    const M3SnapshotMap* map = i_function->snapshotMap;

    if (not map) {
        return "a function in the snapshot was compiled before the runtime was made suspendable";
    }

    for (u32 i = 0; i < map->numRuns; ++i) {
        const M3CodeRun* run = &map->runs[i];

        if (i_pc >= run->start and i_pc < run->start + run->numWords) {
            *o_offset = run->offset + (u32)(i_pc - run->start);
            return m3Err_none;
        }
    }

    for (u32 i = 0; i < map->numRuns; ++i) {
        const M3CodeRun* run = &map->runs[i];

        if (i_pc == run->start + run->numWords) {
            *o_offset = run->offset + run->numWords;
            return m3Err_none;
        }
    }

    return "a pc in the snapshot is not in the code of the function it belongs to";
}

static
M3Result OffsetToPc (IM3Function i_function, u32 i_offset, pc_t* o_pc)
{
    M3Result             result = m3Err_none;
    const M3SnapshotMap* map;

    if (not i_function->compiled) {
_       (CompileFunction(i_function));
    }

    map = i_function->snapshotMap;

    _throwif("a function in the snapshot was compiled before the runtime was made suspendable", not map);

    for (u32 i = 0; i < map->numRuns; ++i) {
        const M3CodeRun* run = &map->runs[i];

        if (i_offset >= run->offset and i_offset < run->offset + run->numWords) {
            *o_pc = run->start + (i_offset - run->offset);
            return m3Err_none;
        }
    }

    for (u32 i = 0; i < map->numRuns; ++i) {
        const M3CodeRun* run = &map->runs[i];

        if (i_offset == run->offset + run->numWords) {
            *o_pc = run->start + run->numWords;
            return m3Err_none;
        }
    }

    _throw("a pc in the snapshot is past the end of its function");

_catch:
    return result;
}

static
const M3SafePoint* FindSafePoint (IM3Function i_function, pc_t i_pc, u8 i_kind)
{
    const M3SnapshotMap* map = i_function->snapshotMap;

    if (map) {
        for (u32 i = 0; i < map->numSafePoints; ++i) {
            if (map->safePoints[i].pc == i_pc and map->safePoints[i].kind == i_kind) {
                return &map->safePoints[i];
            }
        }
    }

    return NULL;
}


//---------------------------------------------------------------------------------------------------------------------------------
//  a suspended continuation's frames
//---------------------------------------------------------------------------------------------------------------------------------

// One function's frame within a suspended continuation, and the safepoint that
// says what it holds
typedef struct M3Activation {
    IM3Function function;
    m3stack_t   sp;
    pc_t        pc;
    u8          kind;           // M3SafePointKind
    m3reg_t*    r0;             // the register this frame goes on with, or NULL where it goes on with none
} M3Activation;

#  define d_m3MaxActivations          (d_m3ContinuationMaxFrames + 1)

// The frames of a suspended continuation, outermost first. A call frame is the
// caller waiting at its call site; the callee is the next frame in. A resume
// frame is the innermost frame there is when the suspension passed through
// one, since the rest of it belongs to the continuation it was running.
// Loop, try and entry frames stand inside a function frame already counted.
static
M3Result ListActivations (IM3Continuation i_cont, M3Activation* o_list, u32* o_count)
{
    M3Result    result   = m3Err_none;
    IM3Function function = i_cont->entryFunction;
    u32         count    = 0;

    for (i32 i = (i32)i_cont->numFrames - 1; i >= 0; --i) {
        M3Frame* frame = &i_cont->frames[i];

        if (frame->kind == frame_call) {
            _throwif("a suspended call does not say what it called", not function);

            M3Activation* act = &o_list[count++];
            act->function     = function;
            act->sp           = frame->sp;
            act->pc           = frame->pc;
            act->kind         = safepoint_call;
            act->r0           = &frame->call.r0;

            function = frame->call.function;
        } else if (frame->kind == frame_resume) {
            _throwif("a suspended resume is not the innermost frame", i != 0);

            M3Activation* act = &o_list[count++];
            act->function     = function;
            act->sp           = frame->sp;
            act->pc           = frame->pc;
            act->kind         = safepoint_resume;
            act->r0           = NULL;       // a replayed resume carries on with clear registers
        }
    }

    if (i_cont->numFrames == 0 or i_cont->frames[0].kind != frame_resume) {
        _throwif("a suspended call does not say what it called", not function);

        M3Activation* act = &o_list[count++];
        act->function     = function;
        act->sp           = i_cont->sp;
        act->pc           = i_cont->pc;
        act->kind         = i_cont->suspendPoint;
        act->r0           = &i_cont->r0;
    }

    *o_count = count;

_catch:
    return result;
}

static
M3Result ContinuationStack (IM3Runtime i_runtime, IM3Continuation i_cont, m3slot_t** o_base, u32* o_numSlots)
{
    if (i_cont == i_runtime->rootContinuation) {
        *o_base     = (m3slot_t*)i_runtime->originStack;
        *o_numSlots = i_runtime->numStackSlots;
    } else {
        *o_base     = i_cont->valStack;
        *o_numSlots = i_cont->numStackSlots;
    }

    return (*o_base) ? m3Err_none : "a suspended continuation has no stack";
}

static
u32 NumPointerSlots (void)
{
    return (u32)((sizeof(void*) + sizeof(m3slot_t) - 1) / sizeof(m3slot_t));
}


//---------------------------------------------------------------------------------------------------------------------------------
//  pointer ids
//---------------------------------------------------------------------------------------------------------------------------------

// Pointers numbered in the order they were first added, with a hash index so
// the lookup does not have to walk them
typedef struct M3PointerIds {
    void** items;
    u32    count;
    u32    capacity;

    u32* index;               // id + 1, or 0 for an empty bucket
    u32  indexCapacity;       // a power of two, kept at least twice count
} M3PointerIds;

static
u32 PointerBucket (void* i_pointer, u32 i_capacity)
{
    u64 bits = (u64)(uintptr_t)i_pointer;

    return (u32)((bits * 0x9E3779B97F4A7C15ULL) >> 32) & (i_capacity - 1);
}

static
u32 PointerIds_Find (const M3PointerIds* i_ids, void* i_pointer)
{
    if (i_ids->indexCapacity) {
        u32 bucket = PointerBucket(i_pointer, i_ids->indexCapacity);

        while (i_ids->index[bucket]) {
            u32 id = i_ids->index[bucket] - 1;

            if (i_ids->items[id] == i_pointer) {
                return id;
            }

            bucket = (bucket + 1) & (i_ids->indexCapacity - 1);
        }
    }

    return d_m3SnapshotNone;
}

static
M3Result PointerIds_Add (M3PointerIds* io_ids, void* i_pointer, u32* o_id)
{
    u32 id = PointerIds_Find(io_ids, i_pointer);

    if (id != d_m3SnapshotNone) {
        *o_id = id;
        return m3Err_none;
    }

    if (io_ids->count == io_ids->capacity) {
        u32    capacity = io_ids->capacity ? io_ids->capacity * 2 : 16;
        void** items    = m3_ReallocArray(void*, io_ids->items, capacity, io_ids->capacity);

        if (not items) {
            return m3Err_mallocFailed;
        }

        io_ids->items    = items;
        io_ids->capacity = capacity;
    }

    if ((io_ids->count + 1) * 2 > io_ids->indexCapacity) {
        u32  capacity = io_ids->indexCapacity ? io_ids->indexCapacity * 2 : 32;
        u32* index    = m3_AllocArray(u32, capacity);

        if (not index) {
            return m3Err_mallocFailed;
        }

        for (u32 i = 0; i < io_ids->count; ++i) {
            u32 bucket = PointerBucket(io_ids->items[i], capacity);

            while (index[bucket]) {
                bucket = (bucket + 1) & (capacity - 1);
            }

            index[bucket] = i + 1;
        }

        m3_Free(io_ids->index);
        io_ids->index         = index;
        io_ids->indexCapacity = capacity;
    }

    id                = io_ids->count++;
    io_ids->items[id] = i_pointer;

    u32 bucket = PointerBucket(i_pointer, io_ids->indexCapacity);

    while (io_ids->index[bucket]) {
        bucket = (bucket + 1) & (io_ids->indexCapacity - 1);
    }

    io_ids->index[bucket] = id + 1;

    *o_id = id;

    return m3Err_none;
}

static
void PointerIds_Free (M3PointerIds* io_ids)
{
    m3_Free(io_ids->items);
    m3_Free(io_ids->index);
    memset(io_ids, 0, sizeof(*io_ids));
}


//---------------------------------------------------------------------------------------------------------------------------------
//  saving
//---------------------------------------------------------------------------------------------------------------------------------

// The snapshot is written twice. The first pass writes nothing and only
// follows references: every continuation and exception it meets is numbered,
// and the ones it numbers are followed in turn until nothing new turns up. The
// second pass writes, and by then every reference has the id it will be
// written as - which the file needs up front, since what a reference names can
// come later in it than the reference does.
typedef struct M3SnapshotSave {
    IM3Runtime       runtime;
    IM3Module        module;
    M3SnapshotWriter writer;
    void*            userdata;

    bool postmortem;
    bool writing;

    M3PointerIds liveContinuations;     // everything the runtime still holds, to check
    M3PointerIds liveExceptions;        //   a reference against before following it
    M3PointerIds continuations;         // what the snapshot carries, by id
    M3PointerIds exceptions;
} M3SnapshotSave;

static
M3Result Put (M3SnapshotSave* s, const void* i_data, size_t i_size)
{
    return s->writing ? s->writer(i_data, i_size, s->userdata) : m3Err_none;
}

#  define d_m3Put(VALUE)              Put(s, &(VALUE), sizeof(VALUE))

static
M3Result PutU8 (M3SnapshotSave* s, u8 i_value)
{
    return d_m3Put(i_value);
}

static
M3Result PutU32 (M3SnapshotSave* s, u32 i_value)
{
    return d_m3Put(i_value);
}

static
M3Result PutU64 (M3SnapshotSave* s, u64 i_value)
{
    return d_m3Put(i_value);
}

static
u32 FunctionIndex (IM3Module i_module, IM3Function i_function)
{
    if (i_function and i_function >= i_module->functions and i_function < i_module->functions + i_module->numFunctions) {
        return (u32)(i_function - i_module->functions);
    }

    return d_m3SnapshotNone;
}

static
u32 FuncTypeIndex (IM3Module i_module, IM3FuncType i_type)
{
    for (u32 i = 0; i < i_module->numFuncTypes; ++i) {
        if (i_module->funcTypes[i] == i_type) {
            return i;
        }
    }

    return d_m3SnapshotNone;
}

static
u32 MemoryIndex (IM3Module i_module, IM3Memory i_memory)
{
    for (u32 i = 0; i < i_module->numMemories; ++i) {
        if (i_module->memories[i] == i_memory) {
            return i;
        }
    }

    return d_m3SnapshotNone;
}

#  if d_m3HasExceptionHandling

// Code refers to a tag as whatever it resolved to, so that is what an
// exception holds
static
IM3Tag TagOfIndex (IM3Module i_module, u32 i_index)
{
    IM3Tag tag = &i_module->tags[i_index];

    return tag->resolved ? tag->resolved : tag;
}

static
u32 TagIndex (IM3Module i_module, IM3Tag i_tag)
{
    for (u32 i = 0; i < i_module->numTags; ++i) {
        if (TagOfIndex(i_module, i) == i_tag) {
            return i;
        }
    }

    return d_m3SnapshotNone;
}

#  endif

// The word a reference is written as: a function index or an object id, or
// d_m3SnapshotNullRef
static
M3Result NameReference (M3SnapshotSave* s, u8 i_type, void* i_reference, u64* o_word)
{
    M3Result result = m3Err_none;
    u32      id     = d_m3SnapshotNone;

    *o_word = d_m3SnapshotNullRef;

    if (not i_reference) {
        return m3Err_none;
    }

    if (i_type == c_m3Type_funcref) {
        id = FunctionIndex(s->module, (IM3Function)i_reference);
        _throwif("a funcref names a function of another module", id == d_m3SnapshotNone);
    } else if (i_type == c_m3Type_externref) {
        _throw("an externref belongs to the host, and cannot be saved");
    }
#  if d_m3HasExceptionHandling
    else if (i_type == c_m3Type_exnref) {
        _throwif("an exnref names an exception that no longer exists",
                 PointerIds_Find(&s->liveExceptions, i_reference) == d_m3SnapshotNone);

        u32 count = s->exceptions.count;
_       (PointerIds_Add(&s->exceptions, i_reference, &id));
        _throwif("internal: an exception was found only while writing", s->writing and s->exceptions.count != count);
    }
#  endif
    else if (i_type == c_m3Type_contref) {
        _throwif("a contref names a continuation that no longer exists",
                 PointerIds_Find(&s->liveContinuations, i_reference) == d_m3SnapshotNone);

        u32 count = s->continuations.count;
_       (PointerIds_Add(&s->continuations, i_reference, &id));
        _throwif("internal: a continuation was found only while writing", s->writing and s->continuations.count != count);
    } else {
        _throw("a reference of an unknown type");
    }

    *o_word = id;

_catch:
    return result;
}

// A value of the given type as the file holds it: a reference by name, a
// 64-bit value whole, anything narrower zero-extended
static
M3Result PutValue (M3SnapshotSave* s, m3type_t i_type, const void* i_where)
{
    M3Result result = m3Err_none;
    u64      word   = 0;

    if (IsRefType(i_type)) {
        void* reference;
        memcpy(&reference, i_where, sizeof(reference));
_       (NameReference(s, BaseTypeOf(i_type), reference, &word));
    } else if (Is64BitType(i_type)) {
        memcpy(&word, i_where, sizeof(u64));
    } else {
        u32 narrow;
        memcpy(&narrow, i_where, sizeof(narrow));
        word = narrow;
    }

_   (PutU64(s, word));

_catch:
    return result;
}

static
M3Result PutMemoryChunks (M3SnapshotSave* s, const u8* i_bytes, size_t i_size)
{
    M3Result result = m3Err_none;
    size_t   cursor = 0;

    while (cursor < i_size) {
        u8     byte = i_bytes[cursor];
        size_t run  = 1;

        while (cursor + run < i_size and i_bytes[cursor + run] == byte) {
            run++;
        }

        if ((byte == 0x00 or byte == 0xFF) and run >= d_m3SnapshotRunThreshold) {
            // the memory is zeroed before it is filled in, so a run of zeros
            // is written as nothing at all
            if (byte == 0xFF) {
_               (PutU8(s, d_m3ChunkFillFF));
_               (PutU32(s, (u32)cursor));
_               (PutU32(s, (u32)run));
            }

            cursor += run;
            continue;
        }

        size_t rawStart = cursor;

        while (cursor < i_size) {
            u8     b      = i_bytes[cursor];
            size_t length = 1;

            while (cursor + length < i_size and i_bytes[cursor + length] == b) {
                length++;
            }

            if ((b == 0x00 or b == 0xFF) and length >= d_m3SnapshotRunThreshold) {
                break;
            }

            cursor += length;
        }

_       (PutU8(s, d_m3ChunkRaw));
_       (PutU32(s, (u32)rawStart));
_       (PutU32(s, (u32)(cursor - rawStart)));
_       (Put(s, i_bytes + rawStart, cursor - rawStart));
    }

_   (PutU8(s, d_m3ChunkEnd));

_catch:
    return result;
}

static
M3Result SaveMemories (M3SnapshotSave* s)
{
    M3Result  result = m3Err_none;
    IM3Module module = s->module;

_   (PutU32(s, module->numMemories));

    for (u32 m = 0; m < module->numMemories; ++m) {
        IM3Memory memory  = module->memories[m];
        bool      hasData = memory and memory->mallocated;

_       (PutU64(s, memory ? memory->numPages : 0));
_       (PutU64(s, memory ? memory->maxPages : 0));
_       (PutU32(s, memory ? Memory_PageSize(memory) : d_m3DefaultMemPageSize));
_       (PutU8(s, hasData));

        // nothing in a memory is a reference, so the pass that follows them
        // has no reason to read one
        if (hasData and s->writing) {
_           (PutMemoryChunks(s, m3MemData(memory->mallocated), (size_t)memory->numPages * Memory_PageSize(memory)));
        }
    }

_catch:
    return result;
}

static
M3Result SaveGlobals (M3SnapshotSave* s)
{
    M3Result  result = m3Err_none;
    IM3Module module = s->module;

_   (PutU32(s, module->numGlobals));

    for (u32 g = 0; g < module->numGlobals; ++g) {
        M3Global* global = &module->globals[g];
        M3Global* cell   = global->resolved ? global->resolved : global;

_       (PutU8(s, BaseTypeOf(global->type)));
_       (PutValue(s, global->type, &cell->i64Value));
    }

_catch:
    return result;
}

static
M3Result SaveTables (M3SnapshotSave* s)
{
    M3Result  result = m3Err_none;
    IM3Module module = s->module;

_   (PutU32(s, module->numTables));

    for (u32 t = 0; t < module->numTables; ++t) {
        IM3Table table = module->tables[t];

_       (PutU8(s, BaseTypeOf(table->type)));
_       (PutU32(s, table->size));

        for (u32 e = 0; e < table->size; ++e) {
            u64 word;
_           (NameReference(s, BaseTypeOf(table->type), table->elements[e], &word));
_           (PutU64(s, word));
        }
    }

_catch:
    return result;
}

static
M3Result SaveSegments (M3SnapshotSave* s)
{
    M3Result  result = m3Err_none;
    IM3Module module = s->module;

_   (PutU32(s, module->numDataSegments));

    for (u32 i = 0; i < module->numDataSegments; ++i) {
_       (PutU8(s, module->dataSegments[i].dropped));
    }

_   (PutU32(s, module->numElementSegments));

    for (u32 i = 0; i < module->numElementSegments; ++i) {
_       (PutU8(s, module->elementSegments[i].dropped));
    }

_catch:
    return result;
}

#  if d_m3HasExceptionHandling

static
M3Result SaveExceptionHeader (M3SnapshotSave* s, M3Exception* i_exception)
{
    M3Result result = m3Err_none;

    u32 tagIndex = TagIndex(s->module, i_exception->tag);
    _throwif("an exception was thrown with another module's tag", tagIndex == d_m3SnapshotNone);

_   (PutU32(s, tagIndex));
_   (PutU32(s, i_exception->numArgs));

_catch:
    return result;
}

static
M3Result SaveExceptionPayload (M3SnapshotSave* s, M3Exception* i_exception)
{
    M3Result    result = m3Err_none;
    IM3FuncType type   = i_exception->tag->type;

    _throwif(m3Err_wasmMalformed, i_exception->numArgs != GetFuncTypeNumParams(type));

    // a payload value is held as a number, zero-extended, rather than laid
    // out the way a slot would hold it
    for (u32 i = 0; i < i_exception->numArgs; ++i) {
        m3type_t argType = GetFuncTypeParamType(type, (u16)i);
        u64      word    = i_exception->args[i];

        if (IsRefType(argType)) {
_           (NameReference(s, BaseTypeOf(argType), (void*)(uintptr_t)word, &word));
        }

_       (PutU64(s, word));
    }

_catch:
    return result;
}

#  endif

static
M3Result PutPc (M3SnapshotSave* s, IM3Function i_function, pc_t i_pc)
{
    M3Result result = m3Err_none;
    u32      offset;

_   (PcToOffset(i_function, i_pc, &offset));
_   (PutU32(s, offset));

_catch:
    return result;
}

// A register as a frame goes on with it: a reference by name when the
// safepoint says it holds one, the raw bits otherwise
static
M3Result PutRegister (M3SnapshotSave* s, const M3SafePoint* i_point, const m3reg_t* i_r0)
{
    M3Result result = m3Err_none;
    u8       type   = i_r0 ? i_point->registerType : c_m3Type_none;
    u64      word   = i_r0 ? (u64)*i_r0 : 0;

    if (type != c_m3Type_none) {
_       (NameReference(s, type, (void*)(uintptr_t)*i_r0, &word));
    }

_   (PutU8(s, type));
_   (PutU64(s, word));

_catch:
    return result;
}

static
M3Result SaveSuspendedBody (M3SnapshotSave* s, IM3Continuation i_cont)
{
    M3Result      result      = m3Err_none;
    IM3Module     module      = s->module;
    M3Activation* activations = NULL;
    u8*           stackCopy   = NULL;
    m3slot_t*     base;
    u32           capacity;
    u32           numActivations;
    u32           numSlots       = 0;
    u32           numRelocations = 0;
    u32           pointerSlots   = NumPointerSlots();
    f64           fp0            = 0.;
    IM3Function   function       = i_cont->entryFunction;
    M3Activation* top;
    bool          hasOwnPc;

_   (ContinuationStack(s->runtime, i_cont, &base, &capacity));

    activations = m3_AllocArray(M3Activation, d_m3MaxActivations);
    _throwifnull(activations);

_   (ListActivations(i_cont, activations, &numActivations));

    // Every frame must be this module's and have a map, and the stack is saved
    // up to the furthest any of them can reach

    for (u32 a = 0; a < numActivations; ++a) {
        M3Activation* act = &activations[a];

        _throwif("a suspended frame belongs to another module", FunctionIndex(module, act->function) == d_m3SnapshotNone);
        _throwif("a function in the snapshot was compiled before the runtime was made suspendable", not act->function->snapshotMap);
        _throwif("a suspended frame is not at a safepoint", not FindSafePoint(act->function, act->pc, act->kind));
        _throwif("a suspended frame is outside its stack", act->sp < base or act->sp > base + capacity);

        u32 reach = (u32)(act->sp - base) + act->function->maxStackSlots;
        numSlots  = M3_MAX(numSlots, reach);
    }

    numSlots = M3_MIN(numSlots, capacity);

    // The references go out by name, and the slots they were in go out empty:
    // the file carries no addresses

    if (s->writing) {
        stackCopy = m3_AllocArray(u8, (size_t)numSlots * sizeof(m3slot_t) + 1);
        _throwifnull(stackCopy);
        memcpy(stackCopy, base, (size_t)numSlots * sizeof(m3slot_t));
    }

    for (u32 a = 0; a < numActivations; ++a) {
        M3Activation*      act   = &activations[a];
        const M3SafePoint* point = FindSafePoint(act->function, act->pc, act->kind);
        u32                frame = (u32)(act->sp - base);

        for (u16 r = 0; r < point->numRefs; ++r) {
            const M3SlotRef* ref  = &act->function->snapshotMap->refs[point->firstRef + r];
            u32              slot = frame + ref->slot;

            _throwif("a reference is outside the saved stack", slot + pointerSlots > numSlots);

            if (stackCopy) {
                memset(stackCopy + (size_t)slot * sizeof(m3slot_t), 0, sizeof(void*));
            }

            numRelocations++;
        }
    }

_   (PutU32(s, numSlots));
_   (Put(s, stackCopy, s->writing ? (size_t)numSlots * sizeof(m3slot_t) : 0));
_   (PutU32(s, numRelocations));

    for (u32 a = 0; a < numActivations; ++a) {
        M3Activation*      act   = &activations[a];
        const M3SafePoint* point = FindSafePoint(act->function, act->pc, act->kind);
        u32                frame = (u32)(act->sp - base);

        for (u16 r = 0; r < point->numRefs; ++r) {
            const M3SlotRef* ref  = &act->function->snapshotMap->refs[point->firstRef + r];
            u32              slot = frame + ref->slot;
            void*            reference;
            u64              word;

            memcpy(&reference, base + slot, sizeof(reference));

_           (NameReference(s, ref->type, reference, &word));
_           (PutU32(s, slot));
_           (PutU8(s, ref->type));
_           (PutU64(s, word));
        }
    }

    // Where the continuation goes on from. A suspension that passed through a
    // resume goes on from inside the continuation that resume was running, so
    // this one's own pc is not used.
    top      = &activations[numActivations - 1];
    hasOwnPc = (i_cont->numFrames == 0 or i_cont->frames[0].kind != frame_resume);

_   (PutU8(s, hasOwnPc));

    if (hasOwnPc) {
_       (PutU8(s, i_cont->suspendPoint));
_       (PutU32(s, FunctionIndex(module, top->function)));
_       (PutPc(s, top->function, i_cont->pc));
_       (PutU32(s, (u32)(i_cont->sp - base)));
_       (PutRegister(s, FindSafePoint(top->function, top->pc, top->kind), &i_cont->r0));
    }

#  if d_m3HasFloat
    fp0 = i_cont->fp0;
#  endif
_   (d_m3Put(fp0));

    // the slots a suspend is waiting on are the frame's own, and already typed
_   (PutU32(s, i_cont->numSuspendResults));

    for (u32 i = 0; i < i_cont->numSuspendResults; ++i) {
_       (PutU32(s, (u32)i_cont->suspendResultOffsets[i]));
_       (PutU8(s, (u8)i_cont->suspendResultIs64[i]));
    }

    // The frames, outermost first. Each pc is in the function the frame
    // stands in, which a call frame hands on to its callee.
_   (PutU32(s, i_cont->numFrames));

    for (i32 i = (i32)i_cont->numFrames - 1; i >= 0; --i) {
        M3Frame* frame = &i_cont->frames[i];

        // a module without memories runs against a placeholder, written as none
        u32 memoryIndex = MemoryIndex(module, frame->memory);
        _throwif("a suspended frame runs against another module's memory",
                 memoryIndex == d_m3SnapshotNone and frame->memory != &module->emptyMemory);
        _throwif("a suspended frame is outside its stack", frame->sp < base or frame->sp > base + numSlots);

        u8 kind = 0;

        switch ((M3FrameKind)frame->kind) {
        case frame_call: kind = snapshot_frameCall; break;
        case frame_loop: kind = snapshot_frameLoop; break;
#  if d_m3HasExceptionHandling
        case frame_try: kind = snapshot_frameTry; break;
#  endif
#  if d_m3EntryKeepsFrame
        case frame_entry: kind = snapshot_frameEntry; break;
#  endif
        case frame_resume: kind = snapshot_frameResume; break;
        }

_       (PutU8(s, kind));
_       (PutU32(s, (u32)(frame->sp - base)));
_       (PutU32(s, memoryIndex));
_       (PutU32(s, FunctionIndex(module, function)));

        switch ((M3FrameKind)frame->kind) {
        case frame_call: {
            const M3SafePoint* point = FindSafePoint(function, frame->pc, safepoint_call);

            f64 callFp0 = 0.;
#  if d_m3HasFloat
            callFp0 = frame->call.fp0;
#  endif

_           (PutPc(s, function, frame->pc));
_           (PutU32(s, FunctionIndex(module, frame->call.function)));
_           (PutRegister(s, point, &frame->call.r0));
_           (d_m3Put(callFp0));

            function = frame->call.function;
            break;
        }

        case frame_loop:
_           (PutPc(s, function, frame->pc));
            break;

#  if d_m3HasExceptionHandling
        case frame_try:
_           (PutPc(s, function, frame->pc));
_           (PutU32(s, frame->try_.numClauses));
_           (PutU8(s, frame->try_.handlersLive));
            break;
#  endif

#  if d_m3EntryKeepsFrame
        case frame_entry:
_           (PutU32(s, FunctionIndex(module, frame->entry.function)));
            break;
#  endif

        case frame_resume: {
            u64 word;
_           (NameReference(s, c_m3Type_contref, frame->resume.cont, &word));
            _throwif("a suspended resume runs no continuation", word == d_m3SnapshotNullRef);

_           (PutPc(s, function, frame->pc));
_           (PutU64(s, word));
_           (PutPc(s, function, frame->resume.handlersPC));
_           (PutU32(s, frame->resume.numHandlers));
_           (PutPc(s, function, frame->resume.resultsPC));
_           (PutU32(s, frame->resume.numResults));
            break;
        }
        }
    }

_catch:
    m3_Free(stackCopy);
    m3_Free(activations);

    return result;
}

static
M3Result SaveContinuation (M3SnapshotSave* s, IM3Continuation i_cont)
{
    M3Result  result      = m3Err_none;
    IM3Module module      = s->module;
    bool      isRoot      = (i_cont == s->runtime->rootContinuation);
    u8        state       = snapshot_contFinished;
    u32       typeIndex   = isRoot ? d_m3SnapshotNone : FuncTypeIndex(module, i_cont->type);
    u64       resumeThrow = d_m3SnapshotNullRef;

    if (i_cont->state == cont_allocated) {
        state = snapshot_contAllocated;
    } else if (i_cont->state == cont_suspended) {
        state = snapshot_contSuspended;
    } else if (i_cont->state == cont_running) {
        _throwif("a continuation is running", not s->postmortem);
    }

    _throwif("a continuation belongs to another module",
             i_cont->entryFunction and FunctionIndex(module, i_cont->entryFunction) == d_m3SnapshotNone);

    _throwif("a continuation has a type of another module", not isRoot and typeIndex == d_m3SnapshotNone);

#  if d_m3HasExceptionHandling
_   (NameReference(s, c_m3Type_exnref, i_cont->resumeThrow, &resumeThrow));
#  endif

_   (PutU8(s, state));
_   (PutU8(s, isRoot));
_   (PutU32(s, typeIndex));
_   (PutU32(s, i_cont->entryFunction ? FunctionIndex(module, i_cont->entryFunction) : d_m3SnapshotNone));
_   (PutU32(s, i_cont->boundArgsCount));
_   (PutU64(s, resumeThrow));

    // a postmortem has nothing to resume, so it does not say how
    if (s->postmortem) {
        goto _catch;
    }

    if (state == snapshot_contAllocated) {
        IM3FuncType inner   = i_cont->type ? i_cont->type->contFuncType : NULL;
        u16         numRets = inner ? GetFuncTypeNumResults(inner) : 0;

        _throwif(m3Err_wasmMalformed, not inner or i_cont->boundArgsCount > GetFuncTypeNumParams(inner));

        for (u32 i = 0; i < i_cont->boundArgsCount; ++i) {
            m3type_t type = GetFuncTypeParamType(inner, (u16)i);

_           (PutValue(s, type, i_cont->valStack + (numRets + i) * c_ioSlotCount));
        }
    } else if (state == snapshot_contSuspended) {
_       (SaveSuspendedBody(s, i_cont));
    }

_catch:
    return result;
}

static
M3Result SaveSections (M3SnapshotSave* s)
{
    M3Result result            = m3Err_none;
    u32      doneContinuations = 0;
    u32      doneExceptions    = 0;

    if (s->writing) {
        u16 byteOrder = d_m3SnapshotByteOrder;
        u32 flags     = s->postmortem ? d_m3SnapshotFlagPostmortem : 0;
        u64 build     = BuildFingerprint();
        u64 module    = ModuleFingerprint(s->module);

_       (Put(s, c_snapshotMagic, sizeof(c_snapshotMagic)));
_       (PutU32(s, flags));
_       (d_m3Put(byteOrder));
_       (PutU8(s, (u8)sizeof(void*)));
_       (PutU8(s, (u8)sizeof(m3slot_t)));
_       (PutU64(s, build));
_       (PutU8(s, IsGasMetered(s->runtime)));
_       (PutU64(s, module));

_       (PutU32(s, s->continuations.count));
_       (PutU32(s, s->exceptions.count));
    }

#  if d_m3HasExceptionHandling
    // each exception's size before any of their contents, so a reader can make
    // all of them before resolving a reference to one
    for (u32 i = 0; i < s->exceptions.count; ++i) {
_       (SaveExceptionHeader(s, (M3Exception*)s->exceptions.items[i]));
    }
#  endif

_   (SaveMemories(s));
_   (SaveGlobals(s));
_   (SaveTables(s));
_   (SaveSegments(s));

    // Following a continuation or an exception can find more of either, so the
    // first pass goes round until neither list grows. The second pass finds
    // nothing new, and goes round once.

    while (doneContinuations < s->continuations.count or doneExceptions < s->exceptions.count) {
#  if d_m3HasExceptionHandling
        while (doneExceptions < s->exceptions.count) {
_           (SaveExceptionPayload(s, (M3Exception*)s->exceptions.items[doneExceptions++]));
        }
#  endif
        while (doneContinuations < s->continuations.count and doneExceptions == s->exceptions.count) {
_           (SaveContinuation(s, (IM3Continuation)s->continuations.items[doneContinuations++]));
        }
    }

_catch:
    return result;
}

#  undef d_m3Put


//---------------------------------------------------------------------------------------------------------------------------------
//  loading
//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3SnapshotLoad {
    IM3Runtime       runtime;
    IM3Module        module;
    M3SnapshotReader reader;
    void*            userdata;

    bool started;              // past the header, and so changing the runtime

    IM3Continuation* continuations;
    u32              numContinuations;

#  if d_m3HasExceptionHandling
    M3Exception** exceptions;
#  endif
    u32 numExceptions;
} M3SnapshotLoad;

static
M3Result Get (M3SnapshotLoad* l, void* o_data, size_t i_size)
{
    return l->reader(o_data, i_size, l->userdata);
}

#  define d_m3Get(VALUE)              Get(l, &(VALUE), sizeof(VALUE))

static
M3Result ResolveReference (M3SnapshotLoad* l, u8 i_type, u64 i_word, void** o_reference)
{
    M3Result result = m3Err_none;

    *o_reference = NULL;

    if (i_word == d_m3SnapshotNullRef) {
        return m3Err_none;
    }

    if (i_type == c_m3Type_funcref) {
        _throwif(m3Err_wasmMalformed, i_word >= l->module->numFunctions);
        *o_reference = &l->module->functions[i_word];
    }
#  if d_m3HasExceptionHandling
    else if (i_type == c_m3Type_exnref) {
        _throwif(m3Err_wasmMalformed, i_word >= l->numExceptions);
        *o_reference = l->exceptions[i_word];
    }
#  endif
    else if (i_type == c_m3Type_contref) {
        _throwif(m3Err_wasmMalformed, i_word >= l->numContinuations);
        *o_reference = l->continuations[i_word];
    } else {
        _throw(m3Err_wasmMalformed);
    }

_catch:
    return result;
}

static
M3Result GetValue (M3SnapshotLoad* l, m3type_t i_type, void* o_where)
{
    M3Result result = m3Err_none;
    u64      word;

_   (d_m3Get(word));

    if (IsRefType(i_type)) {
        void* reference;
_       (ResolveReference(l, BaseTypeOf(i_type), word, &reference));
        memcpy(o_where, &reference, sizeof(reference));
    } else if (Is64BitType(i_type)) {
        memcpy(o_where, &word, sizeof(u64));
    } else {
        u32 narrow = (u32)word;
        memcpy(o_where, &narrow, sizeof(narrow));
    }

_catch:
    return result;
}

static
M3Result GetFunction (M3SnapshotLoad* l, IM3Function* o_function)
{
    M3Result result = m3Err_none;
    u32      index;

_   (d_m3Get(index));

    _throwif(m3Err_wasmMalformed, index >= l->module->numFunctions);
    *o_function = &l->module->functions[index];

_catch:
    return result;
}

static
M3Result GetPc (M3SnapshotLoad* l, IM3Function i_function, pc_t* o_pc)
{
    M3Result result = m3Err_none;
    u32      offset;

_   (d_m3Get(offset));
_   (OffsetToPc(i_function, offset, o_pc));

_catch:
    return result;
}

static
M3Result GetRegister (M3SnapshotLoad* l, m3reg_t* o_r0)
{
    M3Result result = m3Err_none;
    u8       type;
    u64      word;

_   (d_m3Get(type));
_   (d_m3Get(word));

    if (type == c_m3Type_none) {
        *o_r0 = (m3reg_t)word;
    } else {
        void* reference;
_       (ResolveReference(l, type, word, &reference));
        *o_r0 = (m3reg_t)(uintptr_t)reference;
    }

_catch:
    return result;
}

static
M3Result LoadMemoryChunks (M3SnapshotLoad* l, u8* o_bytes, size_t i_size)
{
    M3Result result = m3Err_none;

    for (;;) {
        u8 chunkType;
_       (d_m3Get(chunkType));

        if (chunkType == d_m3ChunkEnd) {
            break;
        }

        u32 offset, length;
_       (d_m3Get(offset));
_       (d_m3Get(length));

        _throwif(m3Err_wasmMalformed, (size_t)offset + length > i_size);

        if (chunkType == d_m3ChunkRaw) {
_           (Get(l, o_bytes + offset, length));
        } else if (chunkType == d_m3ChunkFillFF) {
            memset(o_bytes + offset, 0xFF, length);
        } else {
            _throw(m3Err_wasmMalformed);
        }
    }

_catch:
    return result;
}

static
M3Result LoadMemories (M3SnapshotLoad* l)
{
    M3Result  result = m3Err_none;
    IM3Module module = l->module;
    u32       numMemories;

_   (d_m3Get(numMemories));
    _throwif(m3Err_wasmMalformed, numMemories != module->numMemories);

    for (u32 m = 0; m < numMemories; ++m) {
        u64 numPages, maxPages;
        u32 pageSize;
        u8  hasData;

_       (d_m3Get(numPages));
_       (d_m3Get(maxPages));
_       (d_m3Get(pageSize));
_       (d_m3Get(hasData));

        IM3Memory memory = module->memories[m];

        _throwif(m3Err_wasmMalformed, hasData and not (memory and memory->mallocated));
        _throwif(m3Err_wasmMalformed, memory and pageSize != Memory_PageSize(memory));

        if (hasData) {
            if (memory->numPages < numPages) {
_               (ResizeMemory(l->runtime, memory, numPages));
            }
            _throwif(m3Err_wasmMalformed, memory->numPages != numPages);

            size_t bytes = (size_t)numPages * pageSize;
            u8*    data  = m3MemData(memory->mallocated);

            memset(data, 0, bytes);
_           (LoadMemoryChunks(l, data, bytes));
        }
    }

_catch:
    return result;
}

static
M3Result LoadGlobals (M3SnapshotLoad* l)
{
    M3Result  result = m3Err_none;
    IM3Module module = l->module;
    u32       numGlobals;

_   (d_m3Get(numGlobals));
    _throwif(m3Err_wasmMalformed, numGlobals != module->numGlobals);

    for (u32 g = 0; g < numGlobals; ++g) {
        M3Global* global = &module->globals[g];
        M3Global* cell   = global->resolved ? global->resolved : global;
        u8        type;

_       (d_m3Get(type));
        _throwif(m3Err_wasmMalformed, type != BaseTypeOf(global->type));

_       (GetValue(l, global->type, &cell->i64Value));
    }

_catch:
    return result;
}

static
M3Result LoadTables (M3SnapshotLoad* l)
{
    M3Result  result = m3Err_none;
    IM3Module module = l->module;
    u32       numTables;

_   (d_m3Get(numTables));
    _throwif(m3Err_wasmMalformed, numTables != module->numTables);

    for (u32 t = 0; t < numTables; ++t) {
        IM3Table table = module->tables[t];
        u8       type;
        u32      size;

_       (d_m3Get(type));
_       (d_m3Get(size));

        _throwif(m3Err_wasmMalformed, type != BaseTypeOf(table->type));
        _throwif(m3Err_wasmMalformed, size > (table->maxSize ? table->maxSize : d_m3MaxSaneTableSize));

        // the table can only have grown since it was instantiated
        if (size != table->size) {
            _throwif(m3Err_wasmMalformed, size < table->size);

            void** elements = m3_ReallocArray(void*, table->elements, size, table->size);
            _throwifnull(elements);

            table->elements = elements;
            table->size     = size;
        }

        for (u32 e = 0; e < size; ++e) {
            u64 word;
_           (d_m3Get(word));
_           (ResolveReference(l, type, word, &table->elements[e]));
        }
    }

_catch:
    return result;
}

static
M3Result LoadSegments (M3SnapshotLoad* l)
{
    M3Result  result = m3Err_none;
    IM3Module module = l->module;
    u32       count;

_   (d_m3Get(count));
    _throwif(m3Err_wasmMalformed, count != module->numDataSegments);

    for (u32 i = 0; i < count; ++i) {
        u8 dropped;
_       (d_m3Get(dropped));
        module->dataSegments[i].dropped = dropped;
    }

_   (d_m3Get(count));
    _throwif(m3Err_wasmMalformed, count != module->numElementSegments);

    for (u32 i = 0; i < count; ++i) {
        u8 dropped;
_       (d_m3Get(dropped));
        module->elementSegments[i].dropped = dropped;
    }

_catch:
    return result;
}

// A frame's constants are copied in by op_Entry, and op_Entry already ran
static
void RestoreConstants (const M3Activation* i_activation)
{
    IM3Function function = i_activation->function;

    if (function->constants) {
        u8* where = (u8*)((m3slot_t*)i_activation->sp + function->numRetAndArgSlots) + function->numLocalBytes;

        memcpy(where, function->constants, function->numConstantBytes);
    }
}

static
M3Result LoadSuspendedBody (M3SnapshotLoad* l, IM3Continuation io_cont)
{
    M3Result      result      = m3Err_none;
    IM3Module     module      = l->module;
    M3Activation* activations = NULL;
    m3slot_t*     base;
    u32           capacity;
    u32           numSlots, numRelocations;
    u32           pointerSlots = NumPointerSlots();

_   (ContinuationStack(l->runtime, io_cont, &base, &capacity));

_   (d_m3Get(numSlots));
    _throwif("the snapshot's stack does not fit this runtime's", numSlots > capacity);

_   (Get(l, base, (size_t)numSlots * sizeof(m3slot_t)));

_   (d_m3Get(numRelocations));

    for (u32 i = 0; i < numRelocations; ++i) {
        u32   slot;
        u8    type;
        u64   word;
        void* reference;

_       (d_m3Get(slot));
_       (d_m3Get(type));
_       (d_m3Get(word));

        _throwif(m3Err_wasmMalformed, slot + pointerSlots > numSlots);

_       (ResolveReference(l, type, word, &reference));
        memcpy(base + slot, &reference, sizeof(reference));
    }

    u8 hasOwnPc;
_   (d_m3Get(hasOwnPc));

    io_cont->pc = NULL;
    io_cont->sp = base;
    io_cont->r0 = 0;

    if (hasOwnPc) {
        u8          suspendPoint;
        IM3Function function = NULL;
        u32         spSlot;

_       (d_m3Get(suspendPoint));
        _throwif(m3Err_wasmMalformed, suspendPoint != safepoint_op and suspendPoint != safepoint_suspend);

_       (GetFunction(l, &function));
_       (GetPc(l, function, &io_cont->pc));
_       (d_m3Get(spSlot));
_       (GetRegister(l, &io_cont->r0));

        _throwif(m3Err_wasmMalformed, spSlot > numSlots);

        io_cont->suspendPoint = suspendPoint;
        io_cont->sp           = base + spSlot;
    }

    f64 fp0;
_   (d_m3Get(fp0));
#  if d_m3HasFloat
    io_cont->fp0 = fp0;
#  endif

    u32 numSuspendResults;
_   (d_m3Get(numSuspendResults));
    _throwif(m3Err_wasmMalformed, numSuspendResults > d_m3MaxContinuationPayload);

    io_cont->numSuspendResults = numSuspendResults;

    for (u32 i = 0; i < numSuspendResults; ++i) {
        u32 offset;
        u8  is64;

_       (d_m3Get(offset));
_       (d_m3Get(is64));

        io_cont->suspendResultOffsets[i] = (i32)offset;
        io_cont->suspendResultIs64[i]    = is64;
    }

    u32 numFrames;
_   (d_m3Get(numFrames));
    _throwif(m3Err_wasmMalformed, numFrames > d_m3ContinuationMaxFrames);

    if (numFrames > io_cont->framesCap) {
        M3Frame* frames = m3_ReallocArray(M3Frame, io_cont->frames, numFrames, io_cont->framesCap);
        _throwifnull(frames);

        io_cont->frames    = frames;
        io_cont->framesCap = numFrames;
    }

    io_cont->numFrames = 0;

    for (u32 k = 0; k < numFrames; ++k) {
        // written outermost first, held innermost first
        M3Frame* frame = &io_cont->frames[numFrames - 1 - k];

        u8          kind;
        u32         spSlot, memoryIndex;
        IM3Function function = NULL;

_       (d_m3Get(kind));
_       (d_m3Get(spSlot));
_       (d_m3Get(memoryIndex));
_       (GetFunction(l, &function));

        _throwif(m3Err_wasmMalformed, spSlot > numSlots);
        _throwif(m3Err_wasmMalformed, memoryIndex != d_m3SnapshotNone and memoryIndex >= module->numMemories);
        _throwif(m3Err_wasmMalformed, memoryIndex == d_m3SnapshotNone and module->numMemories);

        memset(frame, 0, sizeof(*frame));
        frame->sp     = base + spSlot;
        frame->memory = (memoryIndex != d_m3SnapshotNone) ? module->memories[memoryIndex] : &module->emptyMemory;

        switch (kind) {
        case snapshot_frameCall: {
            f64 callFp0;

            frame->kind = frame_call;
_           (GetPc(l, function, &frame->pc));
_           (GetFunction(l, &frame->call.function));
_           (GetRegister(l, &frame->call.r0));
_           (d_m3Get(callFp0));
#  if d_m3HasFloat
            frame->call.fp0 = callFp0;
#  endif
            // the callee's code is where the frame inside this one stands
            if (not frame->call.function->compiled) {
_               (CompileFunction(frame->call.function));
            }
            break;
        }

        case snapshot_frameLoop:
            frame->kind = frame_loop;
_           (GetPc(l, function, &frame->pc));
            break;

#  if d_m3HasExceptionHandling
        case snapshot_frameTry: {
            u8 handlersLive;

            frame->kind = frame_try;
_           (GetPc(l, function, &frame->pc));
_           (d_m3Get(frame->try_.numClauses));
_           (d_m3Get(handlersLive));
            frame->try_.handlersLive = handlersLive;
            break;
        }
#  endif

#  if d_m3EntryKeepsFrame
        case snapshot_frameEntry:
            frame->kind = frame_entry;
_           (GetFunction(l, &frame->entry.function));
            break;
#  endif

        case snapshot_frameResume: {
            u64 word;

            frame->kind = frame_resume;
_           (GetPc(l, function, &frame->pc));
_           (d_m3Get(word));
            _throwif(m3Err_wasmMalformed, word == d_m3SnapshotNullRef or word >= l->numContinuations);
            frame->resume.cont = l->continuations[word];
_           (GetPc(l, function, &frame->resume.handlersPC));
_           (d_m3Get(frame->resume.numHandlers));
_           (GetPc(l, function, &frame->resume.resultsPC));
_           (d_m3Get(frame->resume.numResults));
            break;
        }

        default:
            _throw("the snapshot holds a frame this build does not have");
        }
    }

    io_cont->numFrames = numFrames;

    _throwif(m3Err_wasmMalformed, not hasOwnPc and (numFrames == 0 or io_cont->frames[0].kind != frame_resume));

    // every frame has to be at a safepoint of its own function, which is also
    // what says the file and the code it was checked against agree
    activations = m3_AllocArray(M3Activation, d_m3MaxActivations);
    _throwifnull(activations);

    u32 numActivations;
_   (ListActivations(io_cont, activations, &numActivations));

    for (u32 a = 0; a < numActivations; ++a) {
        _throwif("a frame in the snapshot is not at a safepoint",
                 not FindSafePoint(activations[a].function, activations[a].pc, activations[a].kind));

        RestoreConstants(&activations[a]);
    }

_catch:
    m3_Free(activations);

    if (result) {
        io_cont->numFrames = 0;
    }

    return result;
}

static
M3Result LoadContinuation (M3SnapshotLoad* l, u32 i_id)
{
    M3Result        result = m3Err_none;
    IM3Module       module = l->module;
    IM3Continuation cont   = l->continuations[i_id];

    u8  state, isRoot;
    u32 typeIndex, entryIndex, boundArgsCount;
    u64 resumeThrow;

_   (d_m3Get(state));
_   (d_m3Get(isRoot));
_   (d_m3Get(typeIndex));
_   (d_m3Get(entryIndex));
_   (d_m3Get(boundArgsCount));
_   (d_m3Get(resumeThrow));

    _throwif(m3Err_wasmMalformed, (bool)isRoot != (cont == l->runtime->rootContinuation));
    _throwif(m3Err_wasmMalformed, isRoot and state != snapshot_contSuspended);
    _throwif(m3Err_wasmMalformed, typeIndex != d_m3SnapshotNone and typeIndex >= module->numFuncTypes);
    _throwif(m3Err_wasmMalformed, entryIndex != d_m3SnapshotNone and entryIndex >= module->numFunctions);

    cont->type           = (typeIndex != d_m3SnapshotNone) ? module->funcTypes[typeIndex] : NULL;
    cont->entryFunction  = (entryIndex != d_m3SnapshotNone) ? &module->functions[entryIndex] : NULL;
    cont->boundArgsCount = boundArgsCount;
    cont->numHandlers    = 0;
    cont->handlersPC     = NULL;
    cont->parent         = NULL;
    cont->numFrames      = 0;

#  if d_m3HasExceptionHandling
    {
        void* exception;
_       (ResolveReference(l, c_m3Type_exnref, resumeThrow, &exception));
        cont->resumeThrow = (M3Exception*)exception;
    }
#  else
    _throwif(m3Err_wasmMalformed, resumeThrow != d_m3SnapshotNullRef);
#  endif

    if (state == snapshot_contAllocated) {
        IM3FuncType inner = cont->type ? cont->type->contFuncType : NULL;

        _throwif(m3Err_wasmMalformed, not inner or not cont->entryFunction);
        _throwif(m3Err_wasmMalformed, boundArgsCount > GetFuncTypeNumParams(inner));

        u16 numRets = GetFuncTypeNumResults(inner);

        _throwif(m3Err_wasmMalformed, (numRets + boundArgsCount) * c_ioSlotCount > cont->numStackSlots);

        for (u32 i = 0; i < boundArgsCount; ++i) {
_           (GetValue(l, GetFuncTypeParamType(inner, (u16)i), cont->valStack + (numRets + i) * c_ioSlotCount));
        }

        cont->state = cont_allocated;
        cont->sp    = cont->valStack;
        cont->pc    = cont->entryFunction->compiled;
    } else if (state == snapshot_contSuspended) {
        _throwif(m3Err_wasmMalformed, not cont->entryFunction);

        if (not cont->entryFunction->compiled) {
_           (CompileFunction(cont->entryFunction));
        }

_       (LoadSuspendedBody(l, cont));

        cont->state = cont_suspended;
    } else if (state == snapshot_contFinished) {
        cont->state = cont_consumed;
    } else {
        _throw(m3Err_wasmMalformed);
    }

_catch:
    return result;
}

static
M3Result LoadHeader (M3SnapshotLoad* l)
{
    M3Result result = m3Err_none;

    u8  magic[sizeof(c_snapshotMagic)];
    u32 flags;
    u16 byteOrder;
    u8  pointerBytes, slotBytes, gasMetered;
    u64 build, module;

_   (Get(l, magic, sizeof(magic)));
    _throwif(m3Err_wasmMalformed, memcmp(magic, c_snapshotMagic, sizeof(magic)) != 0);

_   (d_m3Get(flags));
_   (d_m3Get(byteOrder));
_   (d_m3Get(pointerBytes));
_   (d_m3Get(slotBytes));
_   (d_m3Get(build));
_   (d_m3Get(gasMetered));
_   (d_m3Get(module));

    _throwif("postmortem snapshots cannot be resumed", flags & d_m3SnapshotFlagPostmortem);
    _throwif("the snapshot was saved on a machine of the other byte order", byteOrder != d_m3SnapshotByteOrder);
    _throwif("the snapshot was saved with another pointer or slot width",
             pointerBytes != sizeof(void*) or slotBytes != sizeof(m3slot_t));
    _throwif("the snapshot was saved by a different build of Wasm3", build != BuildFingerprint());
    _throwif("the snapshot was saved with gas metering set differently", (bool)gasMetered != IsGasMetered(l->runtime));
    _throwif("the snapshot was saved from a different module", module != ModuleFingerprint(l->module));

_catch:
    return result;
}

static
M3Result LoadSections (M3SnapshotLoad* l)
{
    M3Result   result  = m3Err_none;
    IM3Runtime runtime = l->runtime;

_   (LoadHeader(l));

    l->started = true;

_   (d_m3Get(l->numContinuations));
_   (d_m3Get(l->numExceptions));

    _throwif(m3Err_wasmMalformed, l->numContinuations == 0);
#  if !d_m3HasExceptionHandling
    _throwif(m3Err_wasmMalformed, l->numExceptions != 0);
#  endif

    // Every object first, so that a reference to one resolves wherever it is
    // met. The root continuation is the first, and is the runtime's own.
    l->continuations = m3_AllocArray(IM3Continuation, l->numContinuations);
    _throwifnull(l->continuations);

    if (not runtime->rootContinuation) {
        IM3Continuation root = Continuation_New(runtime, NULL, NULL);
        _throwifnull(root);

        // the root runs on the runtime's own stack
        m3_Free(root->valStack);
        root->numStackSlots       = runtime->numStackSlots;
        runtime->rootContinuation = root;
    }

    l->continuations[0] = runtime->rootContinuation;

    for (u32 i = 1; i < l->numContinuations; ++i) {
        l->continuations[i] = Continuation_New(runtime, NULL, NULL);
        _throwifnull(l->continuations[i]);
    }

#  if d_m3HasExceptionHandling
    if (l->numExceptions) {
        l->exceptions = m3_AllocArray(M3Exception*, l->numExceptions);
        _throwifnull(l->exceptions);
    }

    for (u32 i = 0; i < l->numExceptions; ++i) {
        u32 tagIndex, numArgs;

_       (d_m3Get(tagIndex));
_       (d_m3Get(numArgs));

        _throwif(m3Err_wasmMalformed, tagIndex >= l->module->numTags);

        IM3Tag tag = TagOfIndex(l->module, tagIndex);
        _throwif(m3Err_wasmMalformed, numArgs != GetFuncTypeNumParams(tag->type));

        l->exceptions[i] = NewException(runtime, tag, numArgs);
        _throwifnull(l->exceptions[i]);

        l->exceptions[i]->reified = true;
    }
#  endif

_   (LoadMemories(l));
_   (LoadGlobals(l));
_   (LoadTables(l));
_   (LoadSegments(l));

#  if d_m3HasExceptionHandling
    for (u32 i = 0; i < l->numExceptions; ++i) {
        M3Exception* exception = l->exceptions[i];

        for (u32 a = 0; a < exception->numArgs; ++a) {
            m3type_t argType = GetFuncTypeParamType(exception->tag->type, (u16)a);
            u64      word;

_           (d_m3Get(word));

            if (IsRefType(argType)) {
                void* reference;
_               (ResolveReference(l, BaseTypeOf(argType), word, &reference));
                word = (u64)(uintptr_t)reference;
            }

            exception->args[a] = word;
        }
    }
#  endif

    for (u32 i = 0; i < l->numContinuations; ++i) {
_       (LoadContinuation(l, i));
    }

_catch:
    return result;
}

#  undef d_m3Get

#endif // d_m3HasSnapshots


//---------------------------------------------------------------------------------------------------------------------------------
//  API
//---------------------------------------------------------------------------------------------------------------------------------

#if d_m3HasSnapshots

static
M3Result SaveSnapshot (IM3Runtime io_runtime, M3SnapshotWriter i_writer, void* i_userdata)
{
    M3Result       result = m3Err_none;
    M3SnapshotSave save;

    memset(&save, 0, sizeof(save));

    IM3Continuation root = io_runtime->rootContinuation;

    // Anything but a paused invocation is saved as a postmortem: the state of
    // the module the last call entered, which needs no continuation and so no
    // suspendable runtime
    save.runtime    = io_runtime;
    save.writer     = i_writer;
    save.userdata   = i_userdata;
    save.postmortem = not root or root->state != cont_suspended;

    IM3Function entry = save.postmortem ? io_runtime->entered : root->entryFunction;

    if (not entry) {
        return m3Err_none;
    }

    save.module = entry->module;

    for (IM3Continuation cont = io_runtime->continuations; cont; cont = cont->next) {
        u32 id;
_       (PointerIds_Add(&save.liveContinuations, cont, &id));
    }

#  if d_m3HasExceptionHandling
    for (M3Exception* exception = io_runtime->exceptions; exception; exception = exception->next) {
        u32 id;
_       (PointerIds_Add(&save.liveExceptions, exception, &id));
    }
#  endif

    if (not save.postmortem) {
        u32 id;
_       (PointerIds_Add(&save.continuations, root, &id));
    }

_   (SaveSections(&save));

    save.writing = true;

_   (SaveSections(&save));

_catch:
    PointerIds_Free(&save.liveContinuations);
    PointerIds_Free(&save.liveExceptions);
    PointerIds_Free(&save.continuations);
    PointerIds_Free(&save.exceptions);

    return result;
}

#endif

M3Result m3_SaveSnapshot (IM3Runtime io_runtime, M3SnapshotWriter i_writer, void* i_userdata)
{
    if (not io_runtime or not i_writer) {
        return m3Err_mallocFailed;
    }

#if d_m3HasSnapshots
    return SaveSnapshot(io_runtime, i_writer, i_userdata);
#else
    (void)i_userdata;
    return "snapshots are not available in this build of Wasm3";
#endif
}


M3Result m3_LoadSnapshot (IM3Runtime io_runtime, IM3Module i_module, M3SnapshotReader i_reader, void* i_userdata)
{
    M3Result result = m3Err_none;
    _throwifnull(io_runtime);
    _throwifnull(i_module);
    _throwifnull(i_reader);

#if d_m3HasSnapshots
    {
        M3SnapshotLoad load;
        memset(&load, 0, sizeof(load));

        load.runtime  = io_runtime;
        load.module   = i_module;
        load.reader   = i_reader;
        load.userdata = i_userdata;

        // Before anything here compiles a function: what comes out of a
        // snapshot has to be able to suspend again, and a body compiled while
        // this is still false has neither the back edges nor the maps for it
        io_runtime->isSuspendable = true;

        result = LoadSections(&load);

        IM3Continuation root = io_runtime->rootContinuation;

        if (root and load.started) {
            if (result) {
                // whatever did load is not something to resume into
                root->state     = cont_returned;
                root->numFrames = 0;
            } else {
                // A suspension is only ever reached with the start function
                // run, or under way in the invocation being restored, so the
                // memories and globals already hold what it did
                i_module->startFunction = -1;
            }
        }

        Runtime_PlaceCallStack(io_runtime);

        m3_Free(load.continuations);
#  if d_m3HasExceptionHandling
        m3_Free(load.exceptions);
#  endif
    }
#else
    (void)i_userdata;
    _throw("snapshots are not available in this build of Wasm3");
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

    if (i_size) {
        memcpy(bw->buffer + bw->size, i_data, i_size);
        bw->size += i_size;
    }

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

    if (i_size > br->size - br->cursor) {
        return m3Err_wasmMalformed;
    }

    if (i_size) {
        memcpy(o_buffer, br->buffer + br->cursor, i_size);
        br->cursor += i_size;
    }

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
