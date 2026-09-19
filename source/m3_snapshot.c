//
//  m3_snapshot.c
//
//  Copyright © 2026 Volodymyr Shymanskyy. All rights reserved.
//
//  Saving a runtime's execution state to a stream and bringing it back.
//
//  A snapshot is written in Wasm's terms rather than this build's, so it can be
//  resumed by a build on another architecture, with another slot width, another
//  revision, or with gas metering set differently. Every number goes out little
//  endian. A place in a function is the offset of a Wasm instruction; a frame is
//  the function's locals and live operand stack as typed values; a reference is
//  a function index, or the id of a continuation or exception the snapshot
//  carries whole. Nothing in the file is an address, a slot or a metacode word.
//
//  The compiler's snapshot maps (see M3SnapshotMap) are what make this work.
//  Saving reads each value out of wherever this build kept it; loading compiles
//  the same functions, finds the same safepoints in the result, and puts each
//  value wherever that build keeps it - and then stands up the native frames the
//  interpreter needs from what its own code says they hold.
//

#include "m3_env.h"
#include "m3_compile.h"
#include "m3_exception.h"
#include "m3_host.h"

#include <limits.h>

#if d_m3HasSnapshots

//---------------------------------------------------------------------------------------------------------------------------------
//  format
//---------------------------------------------------------------------------------------------------------------------------------

static const u8 c_snapshotMagic[4] = { 'W', '3', 'S', 1 };

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

// The blocks inside a function that the interpreter keeps a native frame for
// while their bodies run
enum {
    snapshot_blockLoop = 1,
    snapshot_blockTry  = 2
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

// The Wasm3 release, and the parts of Wasm it implements: what decides which
// state a program can be in, and so what a snapshot can hold. Nothing that only
// changes how that state is laid out belongs here - slot and pointer widths,
// byte order, gas metering, how the compiler emits code - since none of that
// reaches the file.
static
u64 BuildFingerprint (void)
{
    static const u8 c_features[] = {
        d_m3HasFloat,
        d_m3HasTypedRefs,
        d_m3HasExceptionHandling,
        d_m3HasStackSwitching,
        d_m3HasMultiMemory,
        d_m3HasMemory64,
    };

    u64 hash = HashBytes(d_m3HashSeed, M3_VERSION, sizeof(M3_VERSION));

    return HashBytes(hash, c_features, sizeof(c_features));
}

static
u64 ModuleFingerprint (IM3Module i_module)
{
    size_t size = (i_module->wasmStart and i_module->wasmEnd > i_module->wasmStart)
                    ? (size_t)(i_module->wasmEnd - i_module->wasmStart)
                    : 0;

    return HashBytes(d_m3HashSeed, i_module->wasmStart, size);
}


//---------------------------------------------------------------------------------------------------------------------------------
//  safepoints
//---------------------------------------------------------------------------------------------------------------------------------

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

// How many safepoints of the same kind the same instruction recorded before
// this one. They sit next to each other, since the map is in body order.
static
u32 SafePointOrdinal (const M3SnapshotMap* i_map, const M3SafePoint* i_point)
{
    u32 ordinal = 0;

    for (const M3SafePoint* point = i_point; point > i_map->safePoints; --point) {
        const M3SafePoint* before = point - 1;

        if (before->wasmOffset != i_point->wasmOffset) {
            break;
        }
        if (before->kind == i_point->kind) {
            ordinal++;
        }
    }

    return ordinal;
}

static
const M3SafePoint* FindSafePointAt (const M3SnapshotMap* i_map, u32 i_wasmOffset, u8 i_kind, u32 i_ordinal)
{
    u32 low  = 0;
    u32 high = i_map->numSafePoints;

    while (low < high) {
        u32 middle = low + (high - low) / 2;

        if (i_map->safePoints[middle].wasmOffset < i_wasmOffset) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    for (u32 i = low; i < i_map->numSafePoints and i_map->safePoints[i].wasmOffset == i_wasmOffset; ++i) {
        if (i_map->safePoints[i].kind == i_kind) {
            if (i_ordinal == 0) {
                return &i_map->safePoints[i];
            }
            i_ordinal--;
        }
    }

    return NULL;
}

static
const M3BlockStart* FindBlock (const M3SnapshotMap* i_map, u8 i_opcode, pc_t i_pc)
{
    for (u32 i = 0; i < i_map->numBlocks; ++i) {
        if (i_map->blocks[i].opcode == i_opcode and i_map->blocks[i].pc == i_pc) {
            return &i_map->blocks[i];
        }
    }

    return NULL;
}

static
const M3BlockStart* FindBlockAt (const M3SnapshotMap* i_map, u8 i_opcode, u32 i_wasmOffset)
{
    for (u32 i = 0; i < i_map->numBlocks; ++i) {
        if (i_map->blocks[i].opcode == i_opcode and i_map->blocks[i].wasmOffset == i_wasmOffset) {
            return &i_map->blocks[i];
        }
    }

    return NULL;
}


//---------------------------------------------------------------------------------------------------------------------------------
//  values in a frame
//---------------------------------------------------------------------------------------------------------------------------------

// The registers a frame goes on with. A frame that goes on with clear ones - a
// resume - has none, and a value the map puts in one cannot be there.
typedef struct M3FrameRegisters {
    m3reg_t* r0;
#  if d_m3HasFloat
    f64* fp0;
#  endif
} M3FrameRegisters;

// The stack a continuation's frames are on: the runtime's own for the root
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

// Where a value is kept, or NULL for a register; o_bytes is how much of the
// frame it takes, which has to be inside the stack the frame is on
static
M3Result LocateValue (m3slot_t* i_base, u32 i_numSlots, m3stack_t i_sp, const M3SlotValue* i_value, u8** o_where)
{
    M3Result result = m3Err_none;
    u8       type   = i_value->type;

    *o_where = NULL;

    _throwif("a v128 value cannot be saved", type == c_m3Type_v128);
    _throwif(m3Err_wasmMalformed, type == c_m3Type_none or type > c_m3Type_contref);

    if (not IsRegisterSlotAlias(i_value->slot)) {
        size_t size   = IsRefType(type) ? sizeof(void*) : (Is64BitType(type) ? sizeof(u64) : sizeof(u32));
        size_t offset = (size_t)((m3slot_t*)i_sp - i_base + i_value->slot) * sizeof(m3slot_t);

        _throwif("a value is outside the stack its frame is on", offset + size > (size_t)i_numSlots * sizeof(m3slot_t));

        *o_where = (u8*)i_base + offset;
    }

_catch:
    return result;
}

// A value as the file holds it before a reference is named: a number's bits,
// zero-extended to 64, or the reference itself
static
M3Result ReadValue (u8* i_where, const M3FrameRegisters* i_registers, const M3SlotValue* i_value, u64* o_bits,
                    void** o_reference)
{
    M3Result result = m3Err_none;
    u8       type   = i_value->type;

    *o_bits      = 0;
    *o_reference = NULL;

    if (i_where) {
        if (IsRefType(type)) {
            memcpy(o_reference, i_where, sizeof(void*));
        } else if (Is64BitType(type)) {
            memcpy(o_bits, i_where, sizeof(u64));
        } else {
            u32 narrow;
            memcpy(&narrow, i_where, sizeof(narrow));
            *o_bits = narrow;
        }
    } else if (IsFpRegisterSlotAlias(i_value->slot)) {
#  if d_m3HasFloat
        _throwif("a value is kept in a register the frame does not hold", not i_registers or not i_registers->fp0);

        if (type == c_m3Type_f32) {
            f32 narrow = (f32)*i_registers->fp0;
            u32 bits;
            memcpy(&bits, &narrow, sizeof(bits));
            *o_bits = bits;
        } else {
            _throwif(m3Err_wasmMalformed, type != c_m3Type_f64);
            memcpy(o_bits, i_registers->fp0, sizeof(u64));
        }
#  else
        _throw(m3Err_wasmMalformed);
#  endif
    } else {
        _throwif("a value is kept in a register the frame does not hold", not i_registers or not i_registers->r0);

        m3reg_t r0 = *i_registers->r0;

        if (IsRefType(type)) {
            *o_reference = (void*)(uintptr_t)r0;
        } else if (Is64BitType(type)) {
            *o_bits = (u64)r0;
        } else {
            *o_bits = (u32)r0;
        }
    }

_catch:
    return result;
}

static
M3Result WriteValue (u8* o_where, const M3FrameRegisters* i_registers, const M3SlotValue* i_value, u64 i_bits,
                     void* i_reference)
{
    M3Result result = m3Err_none;
    u8       type   = i_value->type;

    if (o_where) {
        if (IsRefType(type)) {
            memcpy(o_where, &i_reference, sizeof(void*));
        } else if (Is64BitType(type)) {
            memcpy(o_where, &i_bits, sizeof(u64));
        } else {
            u32 narrow = (u32)i_bits;
            memcpy(o_where, &narrow, sizeof(narrow));
        }
    } else if (IsFpRegisterSlotAlias(i_value->slot)) {
#  if d_m3HasFloat
        _throwif("a value is kept in a register the frame does not hold", not i_registers or not i_registers->fp0);

        if (type == c_m3Type_f32) {
            u32 bits = (u32)i_bits;
            f32 narrow;
            memcpy(&narrow, &bits, sizeof(narrow));
            *i_registers->fp0 = narrow;
        } else {
            _throwif(m3Err_wasmMalformed, type != c_m3Type_f64);
            memcpy(i_registers->fp0, &i_bits, sizeof(u64));
        }
#  else
        _throw(m3Err_wasmMalformed);
#  endif
    } else {
        _throwif("a value is kept in a register the frame does not hold", not i_registers or not i_registers->r0);

        if (IsRefType(type)) {
            *i_registers->r0 = (m3reg_t)(uintptr_t)i_reference;
        } else {
            *i_registers->r0 = (m3reg_t)i_bits;
        }
    }

_catch:
    return result;
}

// The value a safepoint lists at i_index: the function's locals first, then
// the operand stack
static
const M3SlotValue* FrameValue (const M3SnapshotMap* i_map, const M3SafePoint* i_point, u32 i_index)
{
    return (i_index < i_map->numLocals) ? &i_map->locals[i_index]
                                        : &i_map->values[i_point->firstValue + i_index - i_map->numLocals];
}

// A frame's constants are copied in by op_Entry, and op_Entry already ran
static
void RestoreConstants (IM3Function i_function, m3stack_t i_sp)
{
    if (i_function->constants) {
        u8* where = (u8*)((m3slot_t*)i_sp + i_function->numRetAndArgSlots) + i_function->numLocalBytes;

        memcpy(where, i_function->constants, i_function->numConstantBytes);
    }
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
//  buffers
//---------------------------------------------------------------------------------------------------------------------------------

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

static
M3Result PutU8 (M3SnapshotSave* s, u8 i_value)
{
    return Put(s, &i_value, sizeof(i_value));
}

static
M3Result PutU32 (M3SnapshotSave* s, u32 i_value)
{
    u8 bytes[4];

    for (u32 i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (u8)(i_value >> (8 * i));
    }

    return Put(s, bytes, sizeof(bytes));
}

static
M3Result PutU64 (M3SnapshotSave* s, u64 i_value)
{
    u8 bytes[8];

    for (u32 i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (u8)(i_value >> (8 * i));
    }

    return Put(s, bytes, sizeof(bytes));
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

// The word a reference is written as: a function index, an object id, the
// embedder's name for an externref, or d_m3SnapshotNullRef
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
        const M3SnapshotHooks* hooks = &s->runtime->snapshotHooks;

        _throwif("an externref belongs to the host, and cannot be saved", not hooks->nameExternRef);

        u64 name = d_m3SnapshotNullRef;
_       (hooks->nameExternRef(hooks->userdata, i_reference, &name));
        _throwif("the host named an externref as null", name == d_m3SnapshotNullRef);

        *o_word = name;
        goto _catch;
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

// A value held in memory - a global's cell, a bound argument - as the file
// holds it: a reference by name, a 64-bit value whole, anything narrower
// zero-extended
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

        // linear memory is little endian whatever the host is, and nothing in
        // it is a reference, so the pass that follows them has no reason to
        // read it
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

// One function's frame within a suspended continuation, as the native frames
// the interpreter recorded describe it
typedef struct M3Activation {
    IM3Function      function;
    m3stack_t        sp;
    pc_t             pc;
    u8               kind;            // M3SafePointKind
    M3FrameRegisters registers;       // the ones it goes on with
    const M3Frame*   resume;          // the resume it waits in, for safepoint_resume
    i32              firstFrame;      // its frames, outermost first: firstFrame down to lastFrame
    i32              lastFrame;
} M3Activation;

// The activation that starts at frame i_first, walking inward: the loops, try
// regions and entry frame standing in the function, up to the call or resume it
// waits on - or, for the innermost, up to the continuation's own suspension
// point.
static
M3Result NextActivation (IM3Module i_module, IM3Continuation i_cont, IM3Function i_function, m3stack_t i_sp,
                         i32 i_first, M3Activation* o_act)
{
    M3Result result = m3Err_none;
    i32      i      = i_first;

    memset(o_act, 0, sizeof(*o_act));

    o_act->function   = i_function;
    o_act->sp         = i_sp;
    o_act->firstFrame = i_first;

    _throwif("a suspended call does not say what it called", not i_function);

    for (; i >= 0; --i) {
        const M3Frame* frame = &i_cont->frames[i];

        _throwif("a suspended frame runs against another module's memory", frame->memory != Module_Memory0(i_module));
        _throwif("a suspended frame is not where its function's frame is", frame->sp != i_sp);

        if (frame->kind == frame_call) {
            o_act->pc           = frame->pc;
            o_act->kind         = safepoint_call;
            o_act->registers.r0 = (m3reg_t*)&frame->call.r0;
#  if d_m3HasFloat
            o_act->registers.fp0 = (f64*)&frame->call.fp0;
#  endif
            break;
        } else if (frame->kind == frame_resume) {
            _throwif("a suspended resume is not the innermost frame", i != 0);

            o_act->pc     = frame->pc;
            o_act->kind   = safepoint_resume;
            o_act->resume = frame;
            break;
        }
#  if d_m3EntryKeepsFrame
        else if (frame->kind == frame_entry) {
            _throwif("a suspended frame is not the function it is entered with", frame->entry.function != i_function);
        }
#  endif
    }

    if (i < 0) {
        _throwif("a suspended frame is not where its function's frame is", i_cont->sp != i_sp);

        o_act->pc           = i_cont->pc;
        o_act->kind         = i_cont->suspendPoint;
        o_act->registers.r0 = &i_cont->r0;
#  if d_m3HasFloat
        o_act->registers.fp0 = &i_cont->fp0;
#  endif
    }

    o_act->lastFrame = i;

_catch:
    return result;
}

static
u32 NumBlocksInActivation (IM3Continuation i_cont, const M3Activation* i_act)
{
    u32 count = 0;

    for (i32 i = i_act->firstFrame; i > i_act->lastFrame; --i) {
        u8 kind = i_cont->frames[i].kind;

#  if d_m3HasExceptionHandling
        count += (kind == frame_try);
#  endif
        count += (kind == frame_loop);
    }

    return count;
}

// A function's frame: which function, the blocks standing in it, the safepoint
// it waits at, and every value it holds there
static
M3Result SaveActivation (M3SnapshotSave* s, IM3Continuation i_cont, m3slot_t* i_base, u32 i_numSlots,
                         const M3Activation* i_act, const M3SafePoint** o_point)
{
    M3Result             result        = m3Err_none;
    IM3Function          function      = i_act->function;
    const M3SnapshotMap* map           = function->snapshotMap;
    u32                  functionIndex = FunctionIndex(s->module, function);
    const M3SafePoint*   point         = FindSafePoint(function, i_act->pc, i_act->kind);
    u32                  numValues;

    _throwif("a suspended frame belongs to another module", functionIndex == d_m3SnapshotNone);
    _throwif("a function in the snapshot was compiled before the runtime was made suspendable", not map);
    _throwif("a suspended frame is not at a safepoint", not point);

    *o_point = point;

_   (PutU32(s, functionIndex));
_   (PutU32(s, NumBlocksInActivation(i_cont, i_act)));

    for (i32 i = i_act->firstFrame; i > i_act->lastFrame; --i) {
        const M3Frame*      frame = &i_cont->frames[i];
        const M3BlockStart* block = NULL;

        if (frame->kind == frame_loop) {
            block = FindBlock(map, c_waOp_loop, frame->pc);
            _throwif("a loop standing in a suspended frame is not one its function has", not block);

_           (PutU8(s, snapshot_blockLoop));
_           (PutU32(s, block->wasmOffset));
        }
#  if d_m3HasExceptionHandling
        else if (frame->kind == frame_try) {
            block = FindBlock(map, c_waOp_tryTable, frame->pc);
            _throwif("a try_table standing in a suspended frame is not one its function has", not block);

_           (PutU8(s, snapshot_blockTry));
_           (PutU32(s, block->wasmOffset));
_           (PutU8(s, frame->try_.handlersLive));
        }
#  endif
    }

_   (PutU8(s, point->kind));
_   (PutU32(s, point->wasmOffset));
_   (PutU32(s, SafePointOrdinal(map, point)));

    numValues = map->numLocals + point->numValues;

_   (PutU32(s, numValues));

    for (u32 v = 0; v < numValues; ++v) {
        const M3SlotValue* value = FrameValue(map, point, v);
        u8*                where;
        u64                word;
        void*              reference;

_       (LocateValue(i_base, i_numSlots, i_act->sp, value, &where));
_       (ReadValue(where, &i_act->registers, value, &word, &reference));

        if (IsRefType(value->type)) {
_           (NameReference(s, value->type, reference, &word));
        }

_       (PutU8(s, value->type));
_       (PutU64(s, word));
    }

    if (point->kind == safepoint_resume) {
        u64 word;
_       (NameReference(s, c_m3Type_contref, i_act->resume->resume.cont, &word));
        _throwif("a suspended resume runs no continuation", word == d_m3SnapshotNullRef);

_       (PutU64(s, word));
    }

_catch:
    return result;
}

// A suspended continuation, as the functions it is in the middle of, outermost
// first. A call leaves the caller waiting and the callee next in; a resume the
// suspension passed through ends the list, since the rest of it belongs to the
// continuation that resume was running.
static
M3Result SaveSuspendedBody (M3SnapshotSave* s, IM3Continuation i_cont)
{
    M3Result    result   = m3Err_none;
    IM3Function function = i_cont->entryFunction;
    u32         numCalls = 0;
    i32         first    = (i32)i_cont->numFrames - 1;
    m3slot_t*   base;
    u32         numSlots;
    m3stack_t   sp;

_   (ContinuationStack(s->runtime, i_cont, &base, &numSlots));

    for (u32 i = 0; i < i_cont->numFrames; ++i) {
        numCalls += (i_cont->frames[i].kind == frame_call);
    }

_   (PutU32(s, numCalls + 1));

    sp = base;

    for (u32 a = 0; a <= numCalls; ++a) {
        M3Activation       act;
        const M3SafePoint* point = NULL;

_       (NextActivation(s->module, i_cont, function, sp, first, &act));
_       (SaveActivation(s, i_cont, base, numSlots, &act, &point));

        bool isInnermost = (a == numCalls);

        _throwif("a suspended frame waits on a call that is not its innermost", isInnermost != (act.kind != safepoint_call));

        if (not isInnermost) {
            const M3Frame* call = &i_cont->frames[act.lastFrame];

            function = call->call.function;
            sp       = act.sp + point->aux;
            first    = act.lastFrame - 1;
        }
    }

_catch:
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

// The embedder's own state, framed by its size so a reader knows where it
// ends. Only the writing pass asks for it: it holds no references to follow.
static
M3Result SaveHostState (M3SnapshotSave* s)
{
    M3Result               result = m3Err_none;
    const M3SnapshotHooks* hooks  = &s->runtime->snapshotHooks;
    M3BufferWriter         bw     = { NULL, 0, 0 };

    if (s->writing and hooks->saveHostState and not s->postmortem) {
_       (hooks->saveHostState(hooks->userdata, BufferWriter_Write, &bw));
    }

_   (PutU64(s, bw.size));
_   (Put(s, bw.buffer, bw.size));

_catch:
    m3_Free(bw.buffer);

    return result;
}

static
M3Result SaveSections (M3SnapshotSave* s)
{
    M3Result result            = m3Err_none;
    u32      doneContinuations = 0;
    u32      doneExceptions    = 0;

    if (s->writing) {
        u32 flags = s->postmortem ? d_m3SnapshotFlagPostmortem : 0;

_       (Put(s, c_snapshotMagic, sizeof(c_snapshotMagic)));
_       (PutU32(s, flags));
_       (PutU64(s, m3_HostTimeMs()));
_       (PutU64(s, BuildFingerprint()));
_       (PutU64(s, ModuleFingerprint(s->module)));
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

_   (SaveHostState(s));

_catch:
    return result;
}


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

    M3Frame* frames;           // a suspended continuation's frames as they are put together, outermost first
} M3SnapshotLoad;

static
M3Result Get (M3SnapshotLoad* l, void* o_data, size_t i_size)
{
    return l->reader(o_data, i_size, l->userdata);
}

static
M3Result GetU8 (M3SnapshotLoad* l, u8* o_value)
{
    return Get(l, o_value, sizeof(*o_value));
}

static
M3Result GetU32 (M3SnapshotLoad* l, u32* o_value)
{
    u8       bytes[4];
    M3Result result = Get(l, bytes, sizeof(bytes));

    *o_value = 0;

    for (u32 i = 0; i < sizeof(bytes); ++i) {
        *o_value |= (u32)bytes[i] << (8 * i);
    }

    return result;
}

static
M3Result GetU64 (M3SnapshotLoad* l, u64* o_value)
{
    u8       bytes[8];
    M3Result result = Get(l, bytes, sizeof(bytes));

    *o_value = 0;

    for (u32 i = 0; i < sizeof(bytes); ++i) {
        *o_value |= (u64)bytes[i] << (8 * i);
    }

    return result;
}

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
    } else if (i_type == c_m3Type_externref) {
        const M3SnapshotHooks* hooks = &l->runtime->snapshotHooks;

        _throwif("the snapshot holds an externref, and nothing here can bind one", not hooks->bindExternRef);
_       (hooks->bindExternRef(hooks->userdata, i_word, o_reference));
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

_   (GetU64(l, &word));

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

_   (GetU32(l, &index));

    _throwif(m3Err_wasmMalformed, index >= l->module->numFunctions);
    *o_function = &l->module->functions[index];

_catch:
    return result;
}

static
M3Result LoadMemoryChunks (M3SnapshotLoad* l, u8* o_bytes, size_t i_size)
{
    M3Result result = m3Err_none;

    for (;;) {
        u8 chunkType;
_       (GetU8(l, &chunkType));

        if (chunkType == d_m3ChunkEnd) {
            break;
        }

        u32 offset, length;
_       (GetU32(l, &offset));
_       (GetU32(l, &length));

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

_   (GetU32(l, &numMemories));
    _throwif(m3Err_wasmMalformed, numMemories != module->numMemories);

    for (u32 m = 0; m < numMemories; ++m) {
        u64 numPages, maxPages;
        u32 pageSize;
        u8  hasData;

_       (GetU64(l, &numPages));
_       (GetU64(l, &maxPages));
_       (GetU32(l, &pageSize));
_       (GetU8(l, &hasData));

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

_   (GetU32(l, &numGlobals));
    _throwif(m3Err_wasmMalformed, numGlobals != module->numGlobals);

    for (u32 g = 0; g < numGlobals; ++g) {
        M3Global* global = &module->globals[g];
        M3Global* cell   = global->resolved ? global->resolved : global;
        u8        type;

_       (GetU8(l, &type));
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

_   (GetU32(l, &numTables));
    _throwif(m3Err_wasmMalformed, numTables != module->numTables);

    for (u32 t = 0; t < numTables; ++t) {
        IM3Table table = module->tables[t];
        u8       type;
        u32      size;

_       (GetU8(l, &type));
_       (GetU32(l, &size));

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
_           (GetU64(l, &word));
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

_   (GetU32(l, &count));
    _throwif(m3Err_wasmMalformed, count != module->numDataSegments);

    for (u32 i = 0; i < count; ++i) {
        u8 dropped;
_       (GetU8(l, &dropped));
        module->dataSegments[i].dropped = dropped;
    }

_   (GetU32(l, &count));
    _throwif(m3Err_wasmMalformed, count != module->numElementSegments);

    for (u32 i = 0; i < count; ++i) {
        u8 dropped;
_       (GetU8(l, &dropped));
        module->elementSegments[i].dropped = dropped;
    }

_catch:
    return result;
}

// A frame for the continuation being put together, outermost first
static
M3Result AddFrame (M3SnapshotLoad* l, u32* io_numFrames, M3FrameKind i_kind, m3stack_t i_sp, M3Frame** o_frame)
{
    if (*io_numFrames >= d_m3ContinuationMaxFrames) {
        return "the snapshot's frames do not fit this build's";
    }

    M3Frame* frame = &l->frames[(*io_numFrames)++];

    memset(frame, 0, sizeof(*frame));
    frame->kind   = (u8)i_kind;
    frame->sp     = i_sp;
    frame->memory = Module_Memory0(l->module);

    *o_frame = frame;

    return m3Err_none;
}

// Compiled, and with a snapshot map: where a function's frame in a snapshot is
// found in this build's code
static
M3Result PrepareFunction (IM3Function i_function)
{
    M3Result result = m3Err_none;

    if (not i_function->compiled) {
_       (CompileFunction(i_function));
    }

    _throwif("a function in the snapshot was compiled before the runtime was made suspendable", not i_function->snapshotMap);

_catch:
    return result;
}

static
M3Result LoadBlocks (M3SnapshotLoad* l, IM3Function i_function, m3stack_t i_sp, u32* io_numFrames)
{
    M3Result             result = m3Err_none;
    const M3SnapshotMap* map    = i_function->snapshotMap;
    u32                  numBlocks;

_   (GetU32(l, &numBlocks));

    for (u32 b = 0; b < numBlocks; ++b) {
        u8       kind;
        u32      wasmOffset;
        M3Frame* frame;

_       (GetU8(l, &kind));
_       (GetU32(l, &wasmOffset));

        if (kind == snapshot_blockLoop) {
            const M3BlockStart* block = FindBlockAt(map, c_waOp_loop, wasmOffset);
            _throwif("a loop in the snapshot is not one its function has", not block);

_           (AddFrame(l, io_numFrames, frame_loop, i_sp, &frame));
            frame->pc = block->pc;
        }
#  if d_m3HasExceptionHandling
        else if (kind == snapshot_blockTry) {
            const M3BlockStart* block = FindBlockAt(map, c_waOp_tryTable, wasmOffset);
            _throwif("a try_table in the snapshot is not one its function has", not block);

            u8 handlersLive;
_           (GetU8(l, &handlersLive));

_           (AddFrame(l, io_numFrames, frame_try, i_sp, &frame));
            frame->pc                = block->pc;
            frame->try_.numClauses   = block->numClauses;
            frame->try_.handlersLive = handlersLive;
        }
#  endif
        else {
            _throw("the snapshot holds a block this build does not have");
        }
    }

_catch:
    return result;
}

// Puts a function's values where this build keeps them at the safepoint
static
M3Result LoadValues (M3SnapshotLoad* l, m3slot_t* i_base, u32 i_numSlots, m3stack_t i_sp,
                     const M3SnapshotMap* i_map, const M3SafePoint* i_point, const M3FrameRegisters* i_registers)
{
    M3Result result = m3Err_none;
    u32      numValues;

_   (GetU32(l, &numValues));
    _throwif("the snapshot's frame does not match what this build compiled", numValues != i_map->numLocals + i_point->numValues);

    for (u32 v = 0; v < numValues; ++v) {
        const M3SlotValue* value = FrameValue(i_map, i_point, v);
        u8                 type;
        u64                word;
        void*              reference = NULL;
        u8*                where;

_       (GetU8(l, &type));
_       (GetU64(l, &word));

        _throwif("the snapshot's frame does not match what this build compiled", type != value->type);

        if (IsRefType(type)) {
_           (ResolveReference(l, type, word, &reference));
        }

_       (LocateValue(i_base, i_numSlots, i_sp, value, &where));
_       (WriteValue(where, i_registers, value, word, reference));
    }

_catch:
    return result;
}

// The functions a suspended continuation is in the middle of, outermost first.
// Each one's frame is found in this build's code, its values put where this
// build keeps them, and the native frames the interpreter needs to stand it
// back up are made from what its own maps say about them.
static
M3Result LoadSuspendedBody (M3SnapshotLoad* l, IM3Continuation io_cont)
{
    M3Result    result    = m3Err_none;
    u32         numFrames = 0;
    M3Frame*    callFrame = NULL;
    IM3Function function  = NULL;
    m3slot_t*   base;
    u32         numSlots;
    u32         numActivations;
    m3stack_t   sp;

_   (ContinuationStack(l->runtime, io_cont, &base, &numSlots));

_   (GetU32(l, &numActivations));
    _throwif(m3Err_wasmMalformed, numActivations == 0 or numActivations > d_m3ContinuationMaxFrames);

    io_cont->pc                = NULL;
    io_cont->r0                = 0;
    io_cont->numSuspendResults = 0;
    io_cont->suspendPoint      = safepoint_op;
#  if d_m3HasFloat
    io_cont->fp0 = 0.;
#  endif

    sp = base;

    for (u32 a = 0; a < numActivations; ++a) {
        bool               isInnermost = (a + 1 == numActivations);
        const M3SafePoint* point;
        M3FrameRegisters   registers;
        u8                 kind;
        u32                wasmOffset, ordinal;

        memset(&registers, 0, sizeof(registers));

_       (GetFunction(l, &function));
        _throwif(m3Err_wasmMalformed, a == 0 and function != io_cont->entryFunction);

_       (PrepareFunction(function));

        const M3SnapshotMap* map = function->snapshotMap;

        _throwif("the snapshot's stack does not fit this runtime's",
                 (u32)(sp - base) + function->maxStackSlots > numSlots);

        // the call the frame outside this one is waiting on
        if (callFrame) {
            callFrame->call.function = function;
        }

#  if d_m3EntryKeepsFrame
        {
            M3Frame* entry;
_           (AddFrame(l, &numFrames, frame_entry, sp, &entry));
            entry->pc             = function->compiled;
            entry->entry.function = function;
        }
#  endif

_       (LoadBlocks(l, function, sp, &numFrames));

_       (GetU8(l, &kind));
_       (GetU32(l, &wasmOffset));
_       (GetU32(l, &ordinal));

        point = FindSafePointAt(map, wasmOffset, kind, ordinal);
        _throwif("a frame in the snapshot is not at a safepoint of this build's", not point);

        if (not isInnermost) {
            _throwif(m3Err_wasmMalformed, kind != safepoint_call);

_           (AddFrame(l, &numFrames, frame_call, sp, &callFrame));
            callFrame->pc = point->pc;
            registers.r0  = &callFrame->call.r0;
#  if d_m3HasFloat
            registers.fp0 = &callFrame->call.fp0;
#  endif
        } else if (kind == safepoint_resume) {
            M3Frame* frame;
            u32      numResults = point->numResults;
            pc_t     resultsPC  = point->pc - 2 * numResults;

_           (AddFrame(l, &numFrames, frame_resume, sp, &frame));
            frame->pc                 = point->pc;
            frame->resume.resultsPC   = resultsPC;
            frame->resume.numResults  = numResults;
            frame->resume.numHandlers = point->aux;
            frame->resume.handlersPC  = resultsPC - 1 - 3 * point->aux;

            // a replayed resume carries on with clear registers, so the frame
            // keeps nothing in them
            callFrame = frame;
        } else {
            _throwif(m3Err_wasmMalformed, kind != safepoint_op and kind != safepoint_gas and kind != safepoint_suspend);

            io_cont->pc           = point->pc;
            io_cont->suspendPoint = kind;
            registers.r0          = &io_cont->r0;
#  if d_m3HasFloat
            registers.fp0 = &io_cont->fp0;
#  endif
        }

_       (LoadValues(l, base, numSlots, sp, map, point, &registers));

        RestoreConstants(function, sp);

        if (not isInnermost) {
            sp = sp + point->aux;
        } else if (kind == safepoint_resume) {
            u64 word;
_           (GetU64(l, &word));
            _throwif(m3Err_wasmMalformed, word == d_m3SnapshotNullRef or word >= l->numContinuations);

            callFrame->resume.cont = l->continuations[word];
        } else {
            // op_ContinueLoopIf suspends before it has branched, and goes on
            // by reading the condition again
            if (point->flags & d_m3SafePointTakenBranch) {
                io_cont->r0 = 1;
            }

            // a suspend waits for its results in the slots at the top of what
            // it holds, which is where whatever resumes it writes them
            if (kind == safepoint_suspend) {
                _throwif(m3Err_wasmMalformed, point->numResults > point->numValues or
                                                point->numResults > d_m3MaxContinuationPayload);

                io_cont->numSuspendResults = point->numResults;

                for (u32 i = 0; i < point->numResults; ++i) {
                    const M3SlotValue* value = &map->values[point->firstValue + point->numValues - point->numResults + i];

                    _throwif(m3Err_wasmMalformed, IsRegisterSlotAlias(value->slot));

                    io_cont->suspendResultOffsets[i] = value->slot;
                    io_cont->suspendResultIs64[i]    = Is64BitType(value->type);
                }
            }
        }
    }

    io_cont->sp = sp;

    // held innermost first
    if (numFrames > io_cont->framesCap) {
        M3Frame* frames = m3_ReallocArray(M3Frame, io_cont->frames, numFrames, io_cont->framesCap);
        _throwifnull(frames);

        io_cont->frames    = frames;
        io_cont->framesCap = numFrames;
    }

    for (u32 k = 0; k < numFrames; ++k) {
        io_cont->frames[numFrames - 1 - k] = l->frames[k];
    }

    io_cont->numFrames = numFrames;

_catch:
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

_   (GetU8(l, &state));
_   (GetU8(l, &isRoot));
_   (GetU32(l, &typeIndex));
_   (GetU32(l, &entryIndex));
_   (GetU32(l, &boundArgsCount));
_   (GetU64(l, &resumeThrow));

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

_       (LoadSuspendedBody(l, cont));

        cont->state = cont_suspended;
    } else if (state == snapshot_contFinished) {
        cont->state = cont_consumed;
    } else {
        _throw(m3Err_wasmMalformed);
    }

_catch:
    if (result) {
        cont->numFrames = 0;
    }

    return result;
}

// Reads the embedder's state through a reader that stops where it ends
typedef struct M3BoundedReader {
    M3SnapshotLoad* load;
    u64             remaining;
} M3BoundedReader;

static
M3Result BoundedReader_Read (void* o_buffer, size_t i_size, void* i_userdata)
{
    M3BoundedReader* br = (M3BoundedReader*)i_userdata;

    if (i_size > br->remaining) {
        return "the host read past the end of its state";
    }

    br->remaining -= i_size;

    return Get(br->load, o_buffer, i_size);
}

static
M3Result LoadHostState (M3SnapshotLoad* l)
{
    M3Result               result = m3Err_none;
    const M3SnapshotHooks* hooks  = &l->runtime->snapshotHooks;
    M3BoundedReader        reader;
    u64                    size;

_   (GetU64(l, &size));

    if (size) {
        _throwif("the snapshot carries host state, and nothing here restores it", not hooks->loadHostState);
        _throwif(m3Err_wasmMalformed, (u64)(size_t)size != size);

        reader.load      = l;
        reader.remaining = size;

_       (hooks->loadHostState(hooks->userdata, BoundedReader_Read, &reader, (size_t)size));
        _throwif("the host did not read all of its state", reader.remaining != 0);
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
    u64 timestamp, build, module;

_   (Get(l, magic, sizeof(magic)));
    _throwif(m3Err_wasmMalformed, memcmp(magic, c_snapshotMagic, sizeof(magic)) != 0);

_   (GetU32(l, &flags));
_   (GetU64(l, &timestamp));
_   (GetU64(l, &build));
_   (GetU64(l, &module));

    (void)timestamp;

    _throwif("postmortem snapshots cannot be resumed", flags & d_m3SnapshotFlagPostmortem);
    _throwif("the snapshot was saved by an incompatible build of Wasm3", build != BuildFingerprint());
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

_   (GetU32(l, &l->numContinuations));
_   (GetU32(l, &l->numExceptions));

    _throwif(m3Err_wasmMalformed, l->numContinuations == 0);
#  if !d_m3HasExceptionHandling
    _throwif("the snapshot holds exceptions, and this build has none", l->numExceptions != 0);
#  endif

    l->frames = m3_AllocArray(M3Frame, d_m3ContinuationMaxFrames);
    _throwifnull(l->frames);

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

_       (GetU32(l, &tagIndex));
_       (GetU32(l, &numArgs));

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

_           (GetU64(l, &word));

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

_   (LoadHostState(l));

_catch:
    return result;
}

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

        m3_Free(load.frames);
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


void m3_SetSnapshotHooks (IM3Runtime io_runtime, const M3SnapshotHooks* i_hooks)
{
#if d_m3HasSnapshots
    if (io_runtime) {
        if (i_hooks) {
            io_runtime->snapshotHooks = *i_hooks;
        } else {
            memset(&io_runtime->snapshotHooks, 0, sizeof(io_runtime->snapshotHooks));
        }
    }
#else
    (void)io_runtime;
    (void)i_hooks;
#endif
}


M3Result m3_SaveSnapshotToBuffer (IM3Runtime io_runtime, void** o_bytes, size_t* o_size)
{
    if (!io_runtime || !o_bytes || !o_size) {
        return m3Err_mallocFailed;
    }
#if d_m3HasSnapshots
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
#else
    *o_bytes = NULL;
    *o_size  = 0;
    return "snapshots are not available in this build of Wasm3";
#endif
}

M3Result m3_LoadSnapshotFromBuffer (IM3Runtime io_runtime, IM3Module i_module, const void* i_bytes, size_t i_size)
{
    if (!io_runtime || !i_module || !i_bytes) {
        return m3Err_mallocFailed;
    }
#if d_m3HasSnapshots
    M3BufferReader br = { (const u8*)i_bytes, i_size, 0 };
    return m3_LoadSnapshot(io_runtime, i_module, BufferReader_Read, &br);
#else
    (void)i_size;
    return "snapshots are not available in this build of Wasm3";
#endif
}
