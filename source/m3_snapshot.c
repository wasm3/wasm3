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

// The container opens the way a Wasm binary does: four bytes of magic and a
// fixed 32-bit version, little endian. Eight bytes, at a fixed place, whatever
// the version turns out to say.
static const u8  c_snapshotMagic[4] = { 0x00, 'd', 'm', 'p' };
static const u32 c_snapshotVersion  = 1;

enum {
    d_m3SnapshotSection_Meta         = 0,
    d_m3SnapshotSection_Memory       = 1,
    d_m3SnapshotSection_Table        = 2,
    d_m3SnapshotSection_Global       = 3,
    d_m3SnapshotSection_Segment      = 4,
    d_m3SnapshotSection_Exception    = 5,
    d_m3SnapshotSection_Continuation = 6,
    d_m3SnapshotSection_HostState    = 7,
};

#  define d_m3SnapshotFlagPostmortem  0x1

// The longest <name> in a "snapshot.<name>" custom section. A name is a label
// the embedder picks, not something the format needs room to grow.
#  define d_m3MaxSnapshotNameLength   200

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

// Memory contents go out as runs, so the long stretches of zero or 0xFF that
// most memories are made of cost a few bytes rather than their length
#  define d_m3ChunkEnd                0x00
#  define d_m3ChunkRaw                0x01
#  define d_m3ChunkFillFF             0x02


//---------------------------------------------------------------------------------------------------------------------------------
//  identity
//---------------------------------------------------------------------------------------------------------------------------------

// Only standard sections affect the module's state. Custom sections (including
// names, debug information and snapshots) can change without changing its identity.
// A module whose bytes are gone has no identity to check, and a snapshot is
// neither saved from one nor loaded into one.
static
M3Result ModuleFingerprint (IM3Module i_module, u64* o_hash)
{
    M3Result result = m3Err_none;
    Xxh64    hash;

    *o_hash = 0;

    _throwif("the module's bytes are gone, so a snapshot cannot tell it from another",
             not i_module->wasmStart or i_module->wasmEnd - i_module->wasmStart < 8);

    {
        bytes_t  pos = i_module->wasmStart + 8;
        cbytes_t end = i_module->wasmEnd;

        Xxh64_Init(&hash, 0);

        while (pos < end) {
            bytes_t start     = pos;
            u8      sectionId = *pos++;
            u32     size      = 0;

_           (ReadLEB_u32(&size, &pos, end));
            _throwif(m3Err_wasmUnderrun, size > (size_t)(end - pos));

            pos += size;
            if (sectionId != 0) {
                Xxh64_Update(&hash, start, (size_t)(pos - start));
            }
        }
    }

    *o_hash = Xxh64_Digest(&hash);

_catch:
    return result;
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

// The safepoint of i_kind at the instruction at i_wasmOffset, and for a back
// edge, the one that goes to the loop at i_targetLoop. Two clauses of one
// instruction can go to the same loop; they leave the same frame, so either of
// their safepoints does.
static
const M3SafePoint* FindSafePointAt (const M3SnapshotMap* i_map, u32 i_wasmOffset, u8 i_kind, u32 i_targetLoop)
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
        const M3SafePoint* point = &i_map->safePoints[i];

        if (point->kind != i_kind or (point->flags & d_m3SafePointTailCall)) {
            continue;
        }
        if (i_kind != safepoint_op or i_map->blocks[point->aux].wasmOffset == i_targetLoop) {
            return point;
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

// Whether a loop or try_table encloses the instruction at i_wasmOffset (see
// M3BlockStart)
static inline
bool EnclosesOffset (const M3BlockStart* i_block, u32 i_wasmOffset)
{
    return i_block->wasmOffset < i_wasmOffset and i_wasmOffset < i_block->wasmEnd;
}

// Whether the handlers of a try_table around a safepoint are in force there.
// A back edge has retired those of the try_tables it leaves: the ones inside
// the loop it goes to.
static inline
bool HandlersLiveAt (const M3SnapshotMap* i_map, const M3SafePoint* i_point, const M3BlockStart* i_try)
{
    return not (i_point->kind == safepoint_op and i_try->wasmOffset > i_map->blocks[i_point->aux].wasmOffset);
}

// The type a call safepoint's instruction calls through, read back out of the
// caller's body: what its callee has to return
static
M3Result CallSiteType (IM3Function i_caller, const M3SafePoint* i_point, IM3FuncType* o_type)
{
    M3Result   result = m3Err_none;
    IM3Module  module = i_caller->module;
    bytes_t    pos    = i_caller->wasm;
    cbytes_t   end    = i_caller->wasmEnd;
    u32        size   = 0;
    u32        index  = 0;
    m3opcode_t opcode = 0;

    *o_type = NULL;

_   (ReadLEB_u32(&size, &pos, end));
    _throwif(m3Err_wasmUnderrun, i_point->wasmOffset >= (size_t)(end - pos));
    pos += i_point->wasmOffset;

_   (Read_opcode(&opcode, &pos, end));
_   (ReadLEB_u32(&index, &pos, end));

    if (opcode == c_waOp_call) {
        _throwif(m3Err_wasmMalformed, index >= module->numFunctions);
        *o_type = module->functions[index].funcType;
    } else {
        _throwif(m3Err_wasmMalformed, opcode != c_waOp_callIndirect and opcode != c_waOp_callRef);
        _throwif(m3Err_wasmMalformed, index >= module->numFuncTypes);
        *o_type = module->funcTypes[index];
    }

_catch:
    return result;
}

// Whether a function can stand where i_expected was called: it returns as
// many values, each a subtype of the one expected - which is what a
// return_call asks of the function it hands its frame to
static
bool ReturnsWhat (IM3FuncType i_type, IM3FuncType i_expected)
{
    u16 numResults = GetFuncTypeNumResults(i_expected);

    if (not i_type or GetFuncTypeNumResults(i_type) != numResults) {
        return false;
    }

    for (u16 i = 0; i < numResults; ++i) {
        if (not IsSubTypeOf(GetFuncTypeResultType(i_type, i), GetFuncTypeResultType(i_expected, i))) {
            return false;
        }
    }

    return true;
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
    u8       type   = BaseTypeOf(i_value->type);

    *o_where = NULL;

    _throwif(m3Err_wasmMalformed, type == c_m3Type_none or type > c_m3Type_contref);

    // no operation reads or writes one, so a v128 only ever sits in slots
    _throwif("a v128 value is kept in a register", type == c_m3Type_v128 and IsRegisterSlotAlias(i_value->slot));

    if (not IsRegisterSlotAlias(i_value->slot)) {
        size_t size   = (type == c_m3Type_v128) ? 16
                                                : (IsRefType(type) ? sizeof(void*)
                                                                   : (Is64BitType(type) ? sizeof(u64) : sizeof(u32)));
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
    u8       type   = BaseTypeOf(i_value->type);

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
    u8       type   = BaseTypeOf(i_value->type);

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
    if (i_index < i_map->numLocals) {
        return i_point->firstLocalValue == UINT32_MAX ? &i_map->locals[i_index]
                                                      : &i_map->values[i_point->firstLocalValue + i_index];
    }
    return &i_map->values[i_point->firstValue + i_index - i_map->numLocals];
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
    bool embedded;
    bool writing;

    M3PointerIds liveContinuations;     // everything the runtime still holds, to check
    M3PointerIds liveExceptions;        //   a reference against before following it
    M3PointerIds continuations;         // what the snapshot carries, by id
    M3PointerIds exceptions;
} M3SnapshotSave;

static inline
u8 M3TypeToValType (m3type_t i_type)
{
    switch (BaseTypeOf(i_type)) {
    case c_m3Type_i32: return 0x7F;
    case c_m3Type_i64: return 0x7E;
    case c_m3Type_f32: return 0x7D;
    case c_m3Type_f64: return 0x7C;
    case c_m3Type_v128: return 0x7B;
    case c_m3Type_funcref: return 0x70;
    case c_m3Type_externref: return 0x6F;
    case c_m3Type_exnref: return 0x69;
    case c_m3Type_contref: return 0x68;
    default: return 0x00;
    }
}

static inline
m3type_t ValTypeToM3Type (u8 i_valtype)
{
    switch (i_valtype) {
    case 0x7F: return c_m3Type_i32;
    case 0x7E: return c_m3Type_i64;
    case 0x7D: return c_m3Type_f32;
    case 0x7C: return c_m3Type_f64;
    case 0x7B: return c_m3Type_v128;
    case 0x70: return c_m3Type_funcref;
    case 0x6F: return c_m3Type_externref;
    case 0x69: return c_m3Type_exnref;
    case 0x68: return c_m3Type_contref;
    default: return c_m3Type_none;
    }
}

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
M3Result PutLEB_u32 (M3SnapshotSave* s, u32 i_value)
{
    u8  bytes[5];
    u32 len = 0;

    do {
        u8 byte = i_value & 0x7F;
        i_value >>= 7;
        if (i_value != 0) {
            byte |= 0x80;
        }
        bytes[len++] = byte;
    } while (i_value != 0);

    return Put(s, bytes, len);
}

static
M3Result PutLEB_u64 (M3SnapshotSave* s, u64 i_value)
{
    u8  bytes[10];
    u32 len = 0;

    do {
        u8 byte = i_value & 0x7F;
        i_value >>= 7;
        if (i_value != 0) {
            byte |= 0x80;
        }
        bytes[len++] = byte;
    } while (i_value != 0);

    return Put(s, bytes, len);
}

static
M3Result PutLEB_i64 (M3SnapshotSave* s, i64 i_value)
{
    u8   bytes[10];
    u32  len  = 0;
    bool more = true;

    while (more) {
        u8 byte = i_value & 0x7F;
        i_value >>= 7;
        if ((i_value == 0 && !(byte & 0x40)) || (i_value == -1 && (byte & 0x40))) {
            more = false;
        } else {
            byte |= 0x80;
        }
        bytes[len++] = byte;
    }

    return Put(s, bytes, len);
}

// What one section writes. Every section body takes the save state and nothing
// else, so SaveSection can drive them all the same way.
typedef M3Result (*M3SnapshotSectionFn)(M3SnapshotSave* s);

// Writes one section: the body into a buffer first, so its length can go in
// front of it, then the whole thing to the real writer. The buffer writer is
// swapped onto s itself rather than onto a copy of it - a body may name a
// reference, and the ids that name one belong to s, not to a copy that is
// about to be thrown away.
static
M3Result SaveSection (M3SnapshotSave* s, u8 i_sectionId, M3SnapshotSectionFn i_body)
{
    M3Result         result   = m3Err_none;
    M3BufferWriter   bw       = { NULL, 0, 0 };
    M3SnapshotWriter writer   = s->writer;
    void*            userdata = s->userdata;

    s->writer   = BufferWriter_Write;
    s->userdata = &bw;

    result = i_body(s);

    s->writer   = writer;
    s->userdata = userdata;

    if (result) {
        goto _catch;
    }

    // a section with nothing in it is left out
    if (bw.size) {
        _throwif("snapshot section is too large", bw.size > UINT32_MAX);
_       (PutU8(s, i_sectionId));
_       (PutLEB_u32(s, (u32)bw.size));
_       (Put(s, bw.buffer, bw.size));
    }

_catch:
    m3_Free(bw.buffer);

    return result;
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

//// A value held in memory - a global's cell, a bound argument - as the file
// holds it: a reference by name, a 64-bit value whole, anything narrower
// zero-extended
static
M3Result PutRefValue (M3SnapshotSave* s, u8 i_type, void* i_reference)
{
    M3Result result = m3Err_none;
    u64      word   = 0;

_   (NameReference(s, i_type, i_reference, &word));

    if (not s->writing) {
        return m3Err_none;
    }

    if (word == d_m3SnapshotNullRef) {
_       (PutU8(s, 0x00));
    } else {
        u8 kind = 0;
        if (i_type == c_m3Type_funcref) {
            kind = 0x01;
        } else if (i_type == c_m3Type_externref) {
            kind = 0x02;
        }
#  if d_m3HasExceptionHandling
        else if (i_type == c_m3Type_exnref) {
            kind = 0x03;
        }
#  endif
        else if (i_type == c_m3Type_contref) {
            kind = 0x04;
        }

_       (PutU8(s, kind));
_       (PutLEB_u64(s, word));
    }

_catch:
    return result;
}

// The fixed-width quantities: the container's version, and a float's bit
// pattern. Little endian whatever the host is. Everything else in the file is
// a LEB128, but a float has to survive as an exact 32- or 64-bit pattern, NaN
// payloads and all, and the version has to sit where a reader can find it
// before it knows anything else about the file.
static
M3Result PutLE32 (M3SnapshotSave* s, u32 i_value)
{
    u8 bytes[4];

    for (u32 i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (u8)(i_value >> (8 * i));
    }

    return Put(s, bytes, sizeof(bytes));
}

static
M3Result PutLE64 (M3SnapshotSave* s, u64 i_value)
{
    u8 bytes[8];

    for (u32 i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (u8)(i_value >> (8 * i));
    }

    return Put(s, bytes, sizeof(bytes));
}

// The body of a value the caller already holds as bits, with a narrow type in
// the low bits - what ReadValue hands back for a frame slot, and what an
// exception payload is. Bits rather than a location: on a big-endian host the
// low half of a u64 is not the half its address points at.
static
M3Result PutValueBody (M3SnapshotSave* s, m3type_t i_type, u64 i_bits)
{
    if (IsRefType(i_type)) {
        return PutRefValue(s, BaseTypeOf(i_type), (void*)(uintptr_t)i_bits);
    }

    switch (BaseTypeOf(i_type)) {
    case c_m3Type_i32: return PutLEB_i64(s, (i32)(u32)i_bits);
    case c_m3Type_i64: return PutLEB_i64(s, (i64)i_bits);
    case c_m3Type_f32: return PutLE32(s, (u32)i_bits);
    case c_m3Type_f64: return PutLE64(s, i_bits);
    default: return "a snapshot cannot hold a value of this type as bits";
    }
}

static
M3Result PutValueTag (M3SnapshotSave* s, m3type_t i_type)
{
    u8 valtype = M3TypeToValType(i_type);

    if (not valtype) {
        return "a snapshot has no tag for a value of this type";
    }

    return PutU8(s, valtype);
}

static
M3Result PutValueBits (M3SnapshotSave* s, m3type_t i_type, u64 i_bits)
{
    M3Result result = m3Err_none;

_   (PutValueTag(s, i_type));
_   (PutValueBody(s, i_type, i_bits));

_catch:
    return result;
}

// A value as a memory location holds it: a global's cell, a bound argument, a
// table element. Narrow types are read at their own width first, so what goes
// on the wire is the value and not the bytes around it.
static
M3Result PutValue (M3SnapshotSave* s, m3type_t i_type, const void* i_where)
{
    M3Result result = m3Err_none;

_   (PutValueTag(s, i_type));

    if (IsRefType(i_type)) {
        void* reference;
        memcpy(&reference, i_where, sizeof(reference));
_       (PutValueBody(s, i_type, (u64)(uintptr_t)reference));
    } else if (BaseTypeOf(i_type) == c_m3Type_v128) {
        // a v128 is a vector of bytes, and has no byte order of its own
        u8 bytes[16];
        memcpy(bytes, i_where, sizeof(bytes));
_       (Put(s, bytes, sizeof(bytes)));
    } else if (Is64BitType(i_type)) {
        u64 bits;
        memcpy(&bits, i_where, sizeof(bits));
_       (PutValueBody(s, i_type, bits));
    } else {
        u32 narrow;
        memcpy(&narrow, i_where, sizeof(narrow));
_       (PutValueBody(s, i_type, narrow));
    }

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
_               (PutLEB_u32(s, (u32)cursor));
_               (PutLEB_u32(s, (u32)run));
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
_       (PutLEB_u32(s, (u32)rawStart));
_       (PutLEB_u32(s, (u32)(cursor - rawStart)));
_       (Put(s, i_bytes + rawStart, cursor - rawStart));
    }

_   (PutU8(s, d_m3ChunkEnd));

_catch:
    return result;
}

// Two imports can name the same memory or table, so one can stand at several
// indices. The file stores it once, at the first of them, and the record at
// any other index names that first one instead.
static
u32 Memory_FirstIndex (IM3Module module, u32 index)
{
    u32 j = 0;
    while (module->memories[j] != module->memories[index]) {
        ++j;
    }
    return j;
}

static
u32 Table_FirstIndex (IM3Module module, u32 index)
{
    u32 j = 0;
    while (module->tables[j] != module->tables[index]) {
        ++j;
    }
    return j;
}

static
M3Result SaveMemories (M3SnapshotSave* s)
{
    M3Result  result = m3Err_none;
    IM3Module module = s->module;

_   (PutLEB_u32(s, module->numMemories));

    for (u32 m = 0; m < module->numMemories; ++m) {
        IM3Memory memory   = module->memories[m];
        u32       first    = Memory_FirstIndex(module, m);
        bool      hasData  = memory and memory->mallocated;
        u32       pageSize = memory ? Memory_PageSize(memory) : d_m3DefaultMemPageSize;
        u64       bytes    = memory ? memory->numPages * (u64)pageSize : 0;
        u32       pageBits = 0;

_       (PutLEB_u32(s, m));
_       (PutLEB_u32(s, first));

        if (first != m) {
            continue;
        }

        // the file counts pages and addresses bytes in 32 bits, whatever size
        // the memory's pages are, so one this large has no encoding here
        _throwif("the memory is too large for a snapshot", bytes > UINT32_MAX);
        _throwif("shrunken memory cannot be saved in a snapshot",
                 memory and memory->mallocated and memory->mallocated->length < bytes);

        // a page size is a power of two, written the way custom page sizes
        // writes one: as that power
        while ((1u << pageBits) < pageSize) {
            ++pageBits;
        }

_       (PutLEB_u32(s, pageBits));
_       (PutLEB_u32(s, memory ? (u32)memory->numPages : 0));

        // linear memory is little endian whatever the host is, and nothing in
        // it is a reference, so the discovery pass has no reason to read it
        if (not s->writing) {
            continue;
        }

        if (hasData) {
_           (PutMemoryChunks(s, m3MemData(memory->mallocated), (size_t)bytes));
        } else {
_           (PutU8(s, d_m3ChunkEnd));
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

_   (PutLEB_u32(s, module->numGlobals));

    for (u32 g = 0; g < module->numGlobals; ++g) {
        M3Global* global = &module->globals[g];
        M3Global* cell   = global->resolved ? global->resolved : global;

_       (PutLEB_u32(s, g));
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

_   (PutLEB_u32(s, module->numTables));

    for (u32 t = 0; t < module->numTables; ++t) {
        IM3Table table = module->tables[t];
        u32      first = Table_FirstIndex(module, t);

_       (PutLEB_u32(s, t));
_       (PutLEB_u32(s, first));

        // the first record holds the elements, and the discovery pass has
        // followed their references there
        if (first != t) {
            continue;
        }

_       (PutU8(s, M3TypeToValType(table->type)));
_       (PutLEB_u32(s, table->size));

        for (u32 e = 0; e < table->size; ++e) {
_           (PutRefValue(s, BaseTypeOf(table->type), table->elements[e]));
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

_   (PutLEB_u32(s, module->numDataSegments));

    for (u32 i = 0; i < module->numDataSegments; ++i) {
_       (PutU8(s, module->dataSegments[i].dropped));
    }

_   (PutLEB_u32(s, module->numElementSegments));

    for (u32 i = 0; i < module->numElementSegments; ++i) {
_       (PutU8(s, module->elementSegments[i].dropped));
    }

_catch:
    return result;
}

#  if d_m3HasExceptionHandling

static
M3Result SaveExceptions (M3SnapshotSave* s)
{
    M3Result result = m3Err_none;

_   (PutLEB_u32(s, s->exceptions.count));

    for (u32 i = 0; i < s->exceptions.count; ++i) {
        M3Exception* exn  = (M3Exception*)s->exceptions.items[i];
        IM3FuncType  type = exn->tag->type;

        _throwif(m3Err_wasmMalformed, exn->numArgs != GetFuncTypeNumParams(type));

_       (PutLEB_u32(s, i));
_       (PutLEB_u32(s, exn->numArgs));

        for (u32 a = 0; a < exn->numArgs; ++a) {
            m3type_t argType = GetFuncTypeParamType(type, (u16)a);
_           (PutValueBits(s, argType, exn->args[a]));
        }
    }

_catch:
    return result;
}

#  endif

// One function's frame within a suspended continuation, as the native frames
// the interpreter recorded describe it
typedef struct M3Activation {
    IM3Function        function;
    const M3SafePoint* point;           // the safepoint it stands at
    m3stack_t          sp;
    pc_t               pc;
    u8                 kind;            // M3SafePointKind
    M3FrameRegisters   registers;       // the ones it goes on with
    const M3Frame*     resume;          // the resume it waits in, for safepoint_resume
    i32                firstFrame;      // its frames, outermost first: firstFrame down to lastFrame
    i32                lastFrame;
} M3Activation;

// The safepoint a frame stands at, and so the function it stands in. That is
// the function the call into it named, io_function, unless a tail call has
// since replaced it; then it is whichever function has a safepoint at i_pc.
static
const M3SafePoint* FindFrameSafePoint (IM3Module i_module, IM3Function* io_function, pc_t i_pc, u8 i_kind)
{
    const M3SafePoint* point = *io_function ? FindSafePoint(*io_function, i_pc, i_kind) : NULL;

    for (u32 i = 0; not point and i < i_module->numFunctions; ++i) {
        point = FindSafePoint(&i_module->functions[i], i_pc, i_kind);
        if (point) {
            *io_function = &i_module->functions[i];
        }
    }

    return point;
}

// The activation that starts at frame i_first, walking inward: the loops, try
// regions and entry frame standing in the function, up to the call or resume it
// waits on - or, for the innermost, up to the continuation's own suspension
// point. i_function is the function the call into it named.
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
    _throwif("a function in the snapshot was compiled before the runtime was made suspendable", not i_function->snapshotMap);

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
    o_act->point     = FindFrameSafePoint(i_module, &o_act->function, o_act->pc, o_act->kind);

    _throwif("a suspended frame is not at a safepoint", not o_act->point);
    _throwif("a function in the snapshot was compiled before the runtime was made suspendable",
             not o_act->function->snapshotMap);

#  if d_m3EntryKeepsFrame
    for (i32 k = i_first; k > o_act->lastFrame; --k) {
        const M3Frame* frame = &i_cont->frames[k];
        _throwif("a suspended frame is not the function it is entered with",
                 frame->kind == frame_entry and frame->entry.function != o_act->function);
    }
#  endif

_catch:
    return result;
}

// A function's frame: which function, the safepoint it waits at, and every
// value it holds there
static
M3Result SaveActivation (M3SnapshotSave* s, m3slot_t* i_base, u32 i_numSlots, const M3Activation* i_act)
{
    M3Result             result        = m3Err_none;
    IM3Function          function      = i_act->function;
    const M3SnapshotMap* map           = function->snapshotMap;
    u32                  functionIndex = FunctionIndex(s->module, function);
    const M3SafePoint*   point         = i_act->point;
    u32                  numValues;

    _throwif("a suspended frame belongs to another module", functionIndex == d_m3SnapshotNone);

_   (PutLEB_u32(s, functionIndex));
_   (PutU8(s, point->kind));
_   (PutLEB_u32(s, point->wasmOffset));

    if (point->kind == safepoint_op) {
_       (PutLEB_u32(s, map->blocks[point->aux].wasmOffset));
    }

    numValues = map->numLocals + point->numValues;

_   (PutLEB_u32(s, numValues));

    for (u32 v = 0; v < numValues; ++v) {
        const M3SlotValue* value = FrameValue(map, point, v);
        u8*                where;
        u64                word;
        void*              reference;

_       (LocateValue(i_base, i_numSlots, i_act->sp, value, &where));

        // a v128 has no byte order of its own, and no bits path can carry it
        if (BaseTypeOf(value->type) == c_m3Type_v128) {
_           (PutValueTag(s, value->type));
_           (Put(s, where, 16));
            continue;
        }

_       (ReadValue(where, &i_act->registers, value, &word, &reference));

_       (PutValueBits(s, value->type, IsRefType(value->type) ? (u64)(uintptr_t)reference : word));
    }

    if (point->kind == safepoint_resume) {
_       (PutRefValue(s, c_m3Type_contref, i_act->resume->resume.cont));
    }

_catch:
    return result;
}

// A suspended continuation, as the functions it is in the middle of, outermost
// first. A call leaves the caller waiting and the callee next in; a resume the
// suspension passed through ends the list, since the rest of it belongs to the
// continuation that resume was running.
//
// A caller left waiting at a return_call compiled as a call is walked past and
// not written: in Wasm its callee has taken its place, and a build that reuses
// the frame has nothing there to write.
static
M3Result SaveSuspendedBody (M3SnapshotSave* s, IM3Continuation i_cont)
{
    M3Result      result   = m3Err_none;
    M3Activation* acts     = NULL;
    u32           numActs  = 0;
    IM3Function   function = i_cont->entryFunction;
    i32           first    = (i32)i_cont->numFrames - 1;
    m3slot_t*     base;
    u32           numSlots;
    m3stack_t     sp;

_   (ContinuationStack(s->runtime, i_cont, &base, &numSlots));

    // one activation for each call frame at most, and the innermost
    acts = m3_AllocArray(M3Activation, i_cont->numFrames + 1);
    _throwifnull(acts);

    sp = base;

    while (true) {
        M3Activation* act = &acts[numActs];

_       (NextActivation(s->module, i_cont, function, sp, first, act));

        if (act->kind != safepoint_call) {
            numActs++;
            break;
        }

        _throwif("a suspended continuation stops at a call", act->lastFrame < 0);

        if (not (act->point->flags & d_m3SafePointTailCall)) {
            numActs++;
        }

        function = i_cont->frames[act->lastFrame].call.function;
        sp       = act->sp + act->point->aux;
        first    = act->lastFrame - 1;
    }

_   (PutLEB_u32(s, numActs));

    for (u32 a = 0; a < numActs; ++a) {
_       (SaveActivation(s, base, numSlots, &acts[a]));
    }

_catch:
    m3_Free(acts);
    return result;
}

static
M3Result SaveContinuation (M3SnapshotSave* s, u32 i_id, IM3Continuation i_cont)
{
    M3Result  result    = m3Err_none;
    IM3Module module    = s->module;
    bool      isRoot    = (i_cont == s->runtime->rootContinuation);
    u8        state     = snapshot_contFinished;
    u32       typeIndex = isRoot ? d_m3SnapshotNone : FuncTypeIndex(module, i_cont->type);

    if (i_cont->state == cont_allocated) {
        state = snapshot_contAllocated;
    } else if (i_cont->state == cont_suspended) {
        state = snapshot_contSuspended;
    } else if (i_cont->state == cont_running) {
        _throwif("a continuation is running", not s->postmortem);
    }

    _throwif("a continuation was made with no function", not i_cont->entryFunction);
    _throwif("a continuation belongs to another module", FunctionIndex(module, i_cont->entryFunction) == d_m3SnapshotNone);

    _throwif("a continuation has a type of another module", not isRoot and typeIndex == d_m3SnapshotNone);

_   (PutLEB_u32(s, i_id));
_   (PutU8(s, state));
_   (PutU8(s, isRoot));
_   (PutLEB_u32(s, typeIndex));
_   (PutLEB_u32(s, FunctionIndex(module, i_cont->entryFunction)));
#  if d_m3HasExceptionHandling
_   (PutRefValue(s, c_m3Type_exnref, i_cont->resumeThrow));
#  else
_   (PutRefValue(s, c_m3Type_exnref, NULL));
#  endif

    // a postmortem has nothing to resume, so it does not say how
    if (s->postmortem) {
        goto _catch;
    }

    if (state == snapshot_contAllocated) {
        IM3FuncType inner   = i_cont->entryFunction ? i_cont->entryFunction->funcType : NULL;
        u16         numRets = inner ? GetFuncTypeNumResults(inner) : 0;

        _throwif(m3Err_wasmMalformed, not inner or i_cont->boundArgsCount > GetFuncTypeNumParams(inner));

_       (PutLEB_u32(s, i_cont->boundArgsCount));

        for (u32 i = 0; i < i_cont->boundArgsCount; ++i) {
            m3type_t type = GetFuncTypeParamType(inner, (u16)i);

_           (PutValue(s, type, i_cont->valStack + (numRets + i) * c_ioSlotCount));
        }
    } else if (state == snapshot_contSuspended) {
_       (PutLEB_u32(s, i_cont->boundArgsCount));
_       (SaveSuspendedBody(s, i_cont));
    } else {
_       (PutLEB_u32(s, 0));
    }

_catch:
    return result;
}

static
M3Result SaveContinuations (M3SnapshotSave* s)
{
    M3Result result = m3Err_none;

_   (PutLEB_u32(s, s->continuations.count));

    for (u32 i = 0; i < s->continuations.count; ++i) {
_       (SaveContinuation(s, i, (IM3Continuation)s->continuations.items[i]));
    }

_catch:
    return result;
}

// The embedder's own state: the whole section, whose length is where it ends.
// Nothing saved is no section at all. Only the writing pass asks for it: it
// holds no references to follow.
static
M3Result SaveHostState (M3SnapshotSave* s)
{
    M3Result               result = m3Err_none;
    const M3SnapshotHooks* hooks  = &s->runtime->snapshotHooks;
    M3BufferWriter         bw     = { NULL, 0, 0 };

    if (s->writing and hooks->saveHostState and not s->postmortem) {
_       (hooks->saveHostState(hooks->userdata, BufferWriter_Write, &bw));
    }

    if (bw.size > 0) {
_       (Put(s, bw.buffer, bw.size));
    }

_catch:
    m3_Free(bw.buffer);

    return result;
}

static
void SnapshotResourceTotals (IM3Module module, u64* memoryBytes, u64* tableElements)
{
    *memoryBytes = *tableElements = 0;
    for (u32 i = 0; i < module->numMemories; ++i) {
        IM3Memory memory = module->memories[i];
        if (Memory_FirstIndex(module, i) == i and memory) {
            *memoryBytes += memory->numPages * (u64)Memory_PageSize(memory);
        }
    }
    for (u32 i = 0; i < module->numTables; ++i) {
        IM3Table table = module->tables[i];
        if (Table_FirstIndex(module, i) == i and table) {
            *tableElements += table->size;
        }
    }
}

static
M3Result SaveMeta (M3SnapshotSave* s)
{
    M3Result result = m3Err_none;
    u64      hash   = 0;
    u64      memoryBytes, tableElements;
    u32      activeStacks = 0;
    for (u32 i = 0; i < s->continuations.count; ++i) {
        IM3Continuation cont = (IM3Continuation)s->continuations.items[i];
        if (cont != s->runtime->rootContinuation and
            (cont->state == cont_allocated or cont->state == cont_suspended)) {
            activeStacks++;
        }
    }
    SnapshotResourceTotals(s->module, &memoryBytes, &tableElements);

_   (ModuleFingerprint(s->module, &hash));

_   (PutLEB_u32(s, s->postmortem ? d_m3SnapshotFlagPostmortem : 0));
_   (PutLEB_u64(s, m3_HostTimeMs()));
_   (PutLEB_u64(s, hash));
_   (PutLEB_u32(s, s->continuations.count));
_   (PutLEB_u32(s, s->exceptions.count));
_   (PutLEB_u64(s, memoryBytes));
_   (PutLEB_u64(s, tableElements));
_   (PutLEB_u32(s, activeStacks));

#  if d_m3HasExceptionHandling
    // each exception's tag before any of their payloads, so a reader can make
    // all of them before resolving a reference to one
    for (u32 i = 0; i < s->exceptions.count; ++i) {
        M3Exception* exception = (M3Exception*)s->exceptions.items[i];
        u32          tagIndex  = TagIndex(s->module, exception->tag);

        _throwif("an exception was thrown with another module's tag", tagIndex == d_m3SnapshotNone);

_       (PutLEB_u32(s, tagIndex));
    }
#  endif

_catch:
    return result;
}

// The discovery pass. Reaching a continuation or an exception can find more of
// either, so it goes round until neither list grows; the writing pass then
// finds nothing new and can lay the sections out in order.
static
M3Result DiscoverReferences (M3SnapshotSave* s)
{
    M3Result result            = m3Err_none;
    u32      doneContinuations = 0;
    u32      doneExceptions    = 0;

_   (SaveMemories(s));
_   (SaveGlobals(s));
_   (SaveTables(s));
_   (SaveSegments(s));

    while (doneContinuations < s->continuations.count or doneExceptions < s->exceptions.count) {
#  if d_m3HasExceptionHandling
        while (doneExceptions < s->exceptions.count) {
            M3Exception* exception = (M3Exception*)s->exceptions.items[doneExceptions++];
            IM3FuncType  type      = exception->tag->type;

            _throwif(m3Err_wasmMalformed, exception->numArgs != GetFuncTypeNumParams(type));

            for (u32 a = 0; a < exception->numArgs; ++a) {
_               (PutValueBits(s, GetFuncTypeParamType(type, (u16)a), exception->args[a]));
            }
        }
#  endif
        while (doneContinuations < s->continuations.count and doneExceptions == s->exceptions.count) {
            u32 id = doneContinuations++;
_           (SaveContinuation(s, id, (IM3Continuation)s->continuations.items[id]));
        }
    }

_catch:
    return result;
}

static
M3Result SaveSections (M3SnapshotSave* s)
{
    M3Result result = m3Err_none;

    if (not s->writing) {
        return DiscoverReferences(s);
    }

    if (not s->embedded) {
_       (Put(s, c_snapshotMagic, sizeof(c_snapshotMagic)));
_       (PutLE32(s, c_snapshotVersion));
    }

    // a section with nothing to say is left out: a module with no memory has
    // no Memory section, rather than one that lists none
_   (SaveSection(s, d_m3SnapshotSection_Meta, SaveMeta));

    if (s->module->numMemories) {
_       (SaveSection(s, d_m3SnapshotSection_Memory, SaveMemories));
    }
    if (s->module->numTables) {
_       (SaveSection(s, d_m3SnapshotSection_Table, SaveTables));
    }
    if (s->module->numGlobals) {
_       (SaveSection(s, d_m3SnapshotSection_Global, SaveGlobals));
    }
    if (s->module->numDataSegments or s->module->numElementSegments) {
_       (SaveSection(s, d_m3SnapshotSection_Segment, SaveSegments));
    }

#  if d_m3HasExceptionHandling
    if (s->exceptions.count) {
_       (SaveSection(s, d_m3SnapshotSection_Exception, SaveExceptions));
    }
#  endif

    if (s->continuations.count) {
_       (SaveSection(s, d_m3SnapshotSection_Continuation, SaveContinuations));
    }

    // left out by SaveSection when the embedder has nothing to add
_   (SaveSection(s, d_m3SnapshotSection_HostState, SaveHostState));

_catch:
    return result;
}

//---------------------------------------------------------------------------------------------------------------------------------
//  loading
//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3SnapshotSuspendSite {
    const M3SnapshotMap* map;
    const M3SafePoint*   point;
    u32                  target;             // child at a resume site, or zero
    bool                 resumed;
    m3type_t             expectedType; // concrete heap type required by references to this record
} M3SnapshotSuspendSite;

typedef struct M3SnapshotLoad {
    IM3Runtime       runtime;
    IM3Module        module;
    M3SnapshotReader reader;
    void*            userdata;

    bool      started;              // past the header, and so changing the runtime
    m3slot_t* checkStack;
    bool      changed;              // past Meta: a section has been applied to the module

    IM3Continuation*       continuations;
    u32                    numContinuations;
    u64                    requiredMemoryBytes;
    u64                    requiredTableElements;
    u64                    loadedMemoryBytes;
    u64                    loadedTableElements;
    u32                    requiredStacks;
    u32                    loadedStacks;
    M3SnapshotSuspendSite* suspendSites;

#  if d_m3HasExceptionHandling
    M3Exception** exceptions;
#  endif
    u32 numExceptions;

    M3Frame* frames;           // a suspended continuation's frames as they are put together, outermost first

    bytes_t pos;               // bounded section payload, read using the Wasm parser's helpers
    bytes_t end;
    bool    inSection;
    bool    checking;
    bool    allowUnfilledRef;  // a suspend's not-yet-bound result slots can contain null
    bool    seenSections[256];
} M3SnapshotLoad;

static
M3Result Get (M3SnapshotLoad* l, void* o_data, size_t i_size)
{
    if (not l->inSection) {
        return l->reader(o_data, i_size, l->userdata);
    }
    if (i_size > (size_t)(l->end - l->pos)) {
        return m3Err_wasmUnderrun;
    }
    if (i_size) {
        memcpy(o_data, l->pos, i_size);
        l->pos += i_size;
    }
    return m3Err_none;
}

static
M3Result GetU8 (M3SnapshotLoad* l, u8* o_value)
{
    return Get(l, o_value, sizeof(*o_value));
}

static
M3Result GetBool (M3SnapshotLoad* l, u8* o_value)
{
    M3Result result = GetU8(l, o_value);
    return result ? result : (*o_value <= 1 ? m3Err_none : m3Err_wasmMalformed);
}

static
M3Result GetLEB_u32 (M3SnapshotLoad* l, u32* o_value)
{
    if (l->inSection) {
        return ReadLEB_u32(o_value, &l->pos, l->end);
    }

    // Only the section envelope is streamed. Collect its length and let the
    // same decoder used for Wasm modules check its width and terminal bits.
    u8 encoded[5];
    for (u32 i = 0; i < sizeof(encoded); ++i) {
        M3Result result = GetU8(l, &encoded[i]);
        if (result) {
            return result;
        }
        if (not (encoded[i] & 0x80)) {
            bytes_t pos = encoded;
            return ReadLEB_u32(o_value, &pos, encoded + i + 1);
        }
    }
    return m3Err_lebOverflow;
}

static
M3Result GetLEB_u64 (M3SnapshotLoad* l, u64* o_value)
{
    return ReadLebUnsigned(o_value, 64, &l->pos, l->end);
}

static
M3Result GetLEB_i64 (M3SnapshotLoad* l, i64* o_value)
{
    return ReadLEB_i64(o_value, &l->pos, l->end);
}

static
M3Result SkipBytes (M3SnapshotLoad* l, size_t i_size)
{
    if (i_size > (size_t)(l->end - l->pos)) {
        return m3Err_wasmUnderrun;
    }
    l->pos += i_size;
    return m3Err_none;
}


static
M3Result GetRefValue (M3SnapshotLoad* l, m3type_t i_type, void** o_reference)
{
    M3Result result = m3Err_none;
    u8       kind   = 0;
    u64      id     = 0;

    *o_reference = NULL;

_   (GetU8(l, &kind));
    if (kind == 0x00) {
        return (IsNullableRef(i_type) or l->allowUnfilledRef) ? m3Err_none : "null in a non-nullable snapshot reference";
    }

_   (GetLEB_u64(l, &id));

    if (kind == 0x01) {
        _throwif(m3Err_wasmMalformed, BaseTypeOf(i_type) != c_m3Type_funcref);
        _throwif(m3Err_wasmMalformed, id >= l->module->numFunctions);
#  if d_m3HasTypedRefs
        _throwif("a snapshot function reference has the wrong type", not IsSubTypeOf(RefTypeOfFuncType(l->module->functions[id].funcType, true), i_type));
#  endif
        *o_reference = &l->module->functions[id];
    } else if (kind == 0x02) {
        _throwif(m3Err_wasmMalformed, BaseTypeOf(i_type) != c_m3Type_externref);
        const M3SnapshotHooks* hooks = &l->runtime->snapshotHooks;
        _throwif("the snapshot holds an externref, and nothing here can bind one", not hooks->bindExternRef);
        if (not l->checking) {
_           (hooks->bindExternRef(hooks->userdata, id, o_reference));
            _throwif(m3Err_wasmMalformed, not *o_reference and not IsNullableRef(i_type));
        }
    }
#  if d_m3HasExceptionHandling
    else if (kind == 0x03) {
        _throwif(m3Err_wasmMalformed, BaseTypeOf(i_type) != c_m3Type_exnref);
        _throwif(m3Err_wasmMalformed, id >= l->numExceptions);
        *o_reference = l->exceptions[id];
    }
#  endif
    else if (kind == 0x04) {
        _throwif(m3Err_wasmMalformed, BaseTypeOf(i_type) != c_m3Type_contref);
        _throwif(m3Err_wasmMalformed, id == 0 or id >= l->numContinuations);
        *o_reference = l->continuations[id];
#  if d_m3HasTypedRefs
        // The record may appear later, even in another section. Check its
        // concrete heap type once all continuation records have been read.
        if (not l->checking and HeapTypeOf(i_type) != d_m3Type_heapAbstract) {
            m3type_t expected = i_type | d_m3Type_refNonNull;
            _throwif(m3Err_wasmMalformed, l->suspendSites[id].expectedType and
                                            l->suspendSites[id].expectedType != expected);
            l->suspendSites[id].expectedType = expected;
        }
#  endif
    } else {
        _throw(m3Err_wasmMalformed);
    }

_catch:
    return result;
}

static
M3Result GetLE32 (M3SnapshotLoad* l, u32* o_value)
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
M3Result GetLE64 (M3SnapshotLoad* l, u64* o_value)
{
    u8       bytes[8];
    M3Result result = Get(l, bytes, sizeof(bytes));

    *o_value = 0;

    for (u32 i = 0; i < sizeof(bytes); ++i) {
        *o_value |= (u64)bytes[i] << (8 * i);
    }

    return result;
}

// Reads the tag of a value and says what it holds, refusing one the file's
// type does not agree with. c_m3Type_none expects anything.
static
M3Result GetValueTag (M3SnapshotLoad* l, m3type_t i_expectedType, m3type_t* o_type)
{
    M3Result result  = m3Err_none;
    u8       valtype = 0;

_   (GetU8(l, &valtype));

    *o_type = ValTypeToM3Type(valtype);

    _throwif(m3Err_wasmMalformed, *o_type == c_m3Type_none);
    _throwif(m3Err_wasmMalformed,
             i_expectedType != c_m3Type_none and BaseTypeOf(*o_type) != BaseTypeOf(i_expectedType));
    if (i_expectedType != c_m3Type_none) {
        *o_type = i_expectedType;
    }

_catch:
    return result;
}

// The mirror of PutValueBody: narrow types come back in the low bits
static
M3Result GetValueBody (M3SnapshotLoad* l, m3type_t i_type, u64* o_bits)
{
    M3Result result = m3Err_none;

    *o_bits = 0;

    if (IsRefType(i_type)) {
        void* reference = NULL;
_       (GetRefValue(l, i_type, &reference));
        *o_bits = (u64)(uintptr_t)reference;
    } else if (BaseTypeOf(i_type) == c_m3Type_i32) {
        i32 value = 0;
_       (ReadLEB_i32(&value, &l->pos, l->end));
        *o_bits = (u32)value;
    } else if (BaseTypeOf(i_type) == c_m3Type_i64) {
        i64 value = 0;
_       (GetLEB_i64(l, &value));
        *o_bits = (u64)value;
    } else if (BaseTypeOf(i_type) == c_m3Type_f32) {
        u32 bits = 0;
_       (GetLE32(l, &bits));
        *o_bits = bits;
    } else if (BaseTypeOf(i_type) == c_m3Type_f64) {
_       (GetLE64(l, o_bits));
    } else {
        _throw(m3Err_wasmMalformed);
    }

_catch:
    return result;
}

static
M3Result GetValueBits (M3SnapshotLoad* l, m3type_t i_expectedType, u64* o_bits)
{
    M3Result result = m3Err_none;
    m3type_t type;

_   (GetValueTag(l, i_expectedType, &type));
_   (GetValueBody(l, type, o_bits));

_catch:
    return result;
}

static
M3Result GetValue (M3SnapshotLoad* l, m3type_t i_expectedType, void* o_where)
{
    M3Result result = m3Err_none;
    m3type_t type;
    u8       scratch[16];
    if (l->checking) {
        o_where = scratch;
    }

_   (GetValueTag(l, i_expectedType, &type));

    if (IsRefType(type)) {
        void* reference = NULL;
        u64   bits      = 0;
_       (GetValueBody(l, type, &bits));
        reference = (void*)(uintptr_t)bits;
        memcpy(o_where, &reference, sizeof(reference));
    } else if (BaseTypeOf(type) == c_m3Type_v128) {
_       (Get(l, o_where, 16));
    } else {
        u64 bits = 0;
_       (GetValueBody(l, type, &bits));

        if (Is64BitType(type)) {
            memcpy(o_where, &bits, sizeof(bits));
        } else {
            u32 narrow = (u32)bits;
            memcpy(o_where, &narrow, sizeof(narrow));
        }
    }

_catch:
    return result;
}

static
M3Result GetFunction (M3SnapshotLoad* l, IM3Function* o_function)
{
    M3Result result = m3Err_none;
    u32      index  = 0;

_   (GetLEB_u32(l, &index));

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
        u8  chunkType = 0;
        u32 offset = 0, length = 0;

_       (GetU8(l, &chunkType));

        if (chunkType == d_m3ChunkEnd) {
            break;
        }

_       (GetLEB_u32(l, &offset));
_       (GetLEB_u32(l, &length));

        _throwif(m3Err_wasmMalformed, (size_t)offset > i_size or length > i_size - (size_t)offset);

        if (chunkType == d_m3ChunkRaw) {
            if (l->checking or length == 0) {
_               (SkipBytes(l, length));
            } else {
_               (Get(l, o_bytes + offset, length));
            }
        } else if (chunkType == d_m3ChunkFillFF) {
            if (not l->checking and length) {
                memset(o_bytes + offset, 0xFF, length);
            }
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
    M3Result  result      = m3Err_none;
    IM3Module module      = l->module;
    u32       numMemories = 0;
    l->loadedMemoryBytes  = 0;

_   (GetLEB_u32(l, &numMemories));
    // a section is present only when it has something to say
    _throwif(m3Err_wasmMalformed, numMemories == 0 or numMemories != module->numMemories);

    for (u32 m = 0; m < numMemories; ++m) {
        u32       memIndex = 0, firstIndex = 0, pageBits = 0, numPages = 0;
        IM3Memory memory;

_       (GetLEB_u32(l, &memIndex));
_       (GetLEB_u32(l, &firstIndex));

        // every memory is listed, in index order, so the index is a check
        _throwif(m3Err_wasmMalformed, memIndex != m);

        // which imports share a memory is settled by linking, not by the
        // module, so the instance restored into has to share the same ones
        _throwif("the snapshot shares memories between imports differently",
                 firstIndex != Memory_FirstIndex(module, m));
        if (firstIndex != m) {
            continue;
        }

_       (GetLEB_u32(l, &pageBits));
_       (GetLEB_u32(l, &numPages));

        memory = module->memories[memIndex];

        size_t bytes = 0;
        u8*    data  = NULL;

        if (memory) {
            _throwif(m3Err_wasmMalformed, pageBits >= 32 or (1u << pageBits) != Memory_PageSize(memory));
            _throwif(m3Err_wasmMalformed, numPages < memory->numPages or numPages > memory->maxPages);
            _throwif(m3Err_wasmMalformed, numPages > SIZE_MAX / Memory_PageSize(memory));
            bytes = (size_t)numPages * Memory_PageSize(memory);

            _throwif("shrunken memory cannot be restored from a snapshot",
                     l->runtime->memoryLimit and bytes > (size_t)l->runtime->memoryLimit);

            if (not l->checking) {
                if (memory->numPages < numPages) {
_                   (ResizeMemory(l->runtime, memory, numPages));
                }
                _throwif("shrunken memory cannot be restored from a snapshot",
                         memory->mallocated and memory->mallocated->length < bytes);
            }

            l->loadedMemoryBytes += bytes;

            if (bytes and memory->mallocated) {
                data = m3MemData(memory->mallocated);
            }
        } else {
            _throwif(m3Err_wasmMalformed, numPages != 0);
        }

        // every memory carries a chunk stream, an unbacked one an empty stream,
        // so a memory this runtime does not hold still has to be read past
        if (data and not l->checking) {
            memset(data, 0, bytes);
        }

_       (LoadMemoryChunks(l, data, bytes));
    }
    _throwif(m3Err_wasmMalformed, l->loadedMemoryBytes != l->requiredMemoryBytes);

_catch:
    return result;
}

static
M3Result LoadGlobals (M3SnapshotLoad* l)
{
    M3Result  result     = m3Err_none;
    IM3Module module     = l->module;
    u32       numGlobals = 0;

_   (GetLEB_u32(l, &numGlobals));
    _throwif(m3Err_wasmMalformed, numGlobals == 0 or numGlobals != module->numGlobals);

    for (u32 g = 0; g < numGlobals; ++g) {
        u32       globalIndex = 0;
        M3Global* global;
        M3Global* cell;

_       (GetLEB_u32(l, &globalIndex));
        _throwif(m3Err_wasmMalformed, globalIndex != g);

        global = &module->globals[globalIndex];
        cell   = global->resolved ? global->resolved : global;

_       (GetValue(l, global->type, &cell->i64Value));
    }

_catch:
    return result;
}

static
M3Result LoadTables (M3SnapshotLoad* l)
{
    M3Result  result       = m3Err_none;
    IM3Module module       = l->module;
    u32       numTables    = 0;
    l->loadedTableElements = 0;

_   (GetLEB_u32(l, &numTables));
    _throwif(m3Err_wasmMalformed, numTables == 0 or numTables != module->numTables);

    for (u32 t = 0; t < numTables; ++t) {
        u32      tableIndex = 0, firstIndex = 0, size = 0;
        u8       valtype = 0;
        IM3Table table;

_       (GetLEB_u32(l, &tableIndex));
_       (GetLEB_u32(l, &firstIndex));

        _throwif(m3Err_wasmMalformed, tableIndex != t);
        _throwif("the snapshot shares tables between imports differently",
                 firstIndex != Table_FirstIndex(module, t));
        if (firstIndex != t) {
            continue;
        }

_       (GetU8(l, &valtype));
_       (GetLEB_u32(l, &size));

        table = module->tables[tableIndex];

        _throwif(m3Err_wasmMalformed, valtype != M3TypeToValType(table->type));
        _throwif(m3Err_wasmMalformed, size > (table->hasMax ? table->maxSize : d_m3MaxSaneTableSize));

        // the table can only have grown since it was instantiated
        _throwif(m3Err_wasmMalformed, size < table->size);

        l->loadedTableElements += size;

        if (not l->checking and size != table->size) {
            void** elements;

            _throwif(m3Err_tableLimitExceeded, l->runtime->tableElementsLimit and
                                                 size - table->size > l->runtime->tableElementsLimit - l->runtime->tableElementsUsed);
            elements = m3_ReallocArray(void*, table->elements, size, table->size);
            _throwifnull(elements);

            l->runtime->tableElementsUsed += size - table->size;
            table->elements = elements;
            table->size     = size;
        }

        for (u32 e = 0; e < size; ++e) {
            void* reference = NULL;
_           (GetRefValue(l, table->type, &reference));
            if (not l->checking) {
                table->elements[e] = reference;
            }
        }
    }
    _throwif(m3Err_wasmMalformed, l->loadedTableElements != l->requiredTableElements);

_catch:
    return result;
}

static
M3Result LoadSegments (M3SnapshotLoad* l)
{
    M3Result  result = m3Err_none;
    IM3Module module = l->module;
    u32       count  = 0;

_   (GetLEB_u32(l, &count));
    _throwif(m3Err_wasmMalformed, count != module->numDataSegments);
    _throwif(m3Err_wasmMalformed, module->numDataSegments == 0 and module->numElementSegments == 0);

    for (u32 i = 0; i < count; ++i) {
        u8 dropped = 0;
_       (GetBool(l, &dropped));
        if (not l->checking) {
            module->dataSegments[i].dropped = dropped;
        }
    }

_   (GetLEB_u32(l, &count));
    _throwif(m3Err_wasmMalformed, count != module->numElementSegments);

    for (u32 i = 0; i < count; ++i) {
        u8 dropped = 0;
_       (GetBool(l, &dropped));
        if (not l->checking) {
            module->elementSegments[i].dropped = dropped;
        }
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

// The native frames the interpreter keeps for the loops and try_tables around
// a safepoint, outermost first. They are fixed by where the frame stands, so
// the snapshot does not list them: they come from this build's own map.
static
M3Result AddBlockFrames (M3SnapshotLoad* l, const M3SnapshotMap* i_map, const M3SafePoint* i_point, m3stack_t i_sp,
                         u32* io_numFrames)
{
    M3Result result = m3Err_none;

    for (u32 b = 0; b < i_map->numBlocks; ++b) {
        const M3BlockStart* block = &i_map->blocks[b];
        M3Frame*            frame;

        if (not EnclosesOffset(block, i_point->wasmOffset)) {
            continue;
        }

        if (block->opcode == c_waOp_loop) {
_           (AddFrame(l, io_numFrames, frame_loop, i_sp, &frame));
            frame->pc = block->pc;
        }
#  if d_m3HasExceptionHandling
        else {
_           (AddFrame(l, io_numFrames, frame_try, i_sp, &frame));
            frame->pc                = block->pc;
            frame->try_.numClauses   = block->numClauses;
            frame->try_.handlersLive = HandlersLiveAt(i_map, i_point, block);
        }
#  endif
    }

_catch:
    return result;
}

// Puts a function's values where this build keeps them at the safepoint
static
M3Result LoadValues (M3SnapshotLoad* l, m3slot_t* i_base, u32 i_numSlots, m3stack_t i_sp,
                     const M3SnapshotMap* i_map, const M3SafePoint* i_point, const M3FrameRegisters* i_registers)
{
    M3Result result    = m3Err_none;
    u32      numValues = 0;

_   (GetLEB_u32(l, &numValues));
    _throwif("the snapshot's frame does not match what this build compiled", numValues != i_map->numLocals + i_point->numValues);

    for (u32 v = 0; v < numValues; ++v) {
        const M3SlotValue* value     = FrameValue(i_map, i_point, v);
        u64                word      = 0;
        void*              reference = NULL;
        u8*                where;

_       (LocateValue(i_base, i_numSlots, i_sp, value, &where));

        // a v128 has no byte order of its own, and no bits path can carry it
        if (BaseTypeOf(value->type) == c_m3Type_v128) {
            m3type_t type;
_           (GetValueTag(l, value->type, &type));
            if (l->checking) {
_               (SkipBytes(l, 16));
            } else {
_               (Get(l, where, 16));
            }
            continue;
        }

        l->allowUnfilledRef = i_point->kind == safepoint_suspend and
                              v >= numValues - i_point->numResults;
_       (GetValueBits(l, value->type, &word));
        l->allowUnfilledRef = false;

        if (IsRefType(value->type)) {
            reference = (void*)(uintptr_t)word;
        }

        if (not l->checking) {
_           (WriteValue(where, i_registers, value, word, reference));
        }
    }

_catch:
    return result;
}

// The functions a suspended continuation is in the middle of, outermost first.
// Each one's frame is found in this build's code, its values put where this
// build keeps them, and the native frames the interpreter needs to stand it
// back up are made from what its own maps say about them.
static
M3Result LoadSuspendedBody (M3SnapshotLoad* l, IM3Continuation io_cont, u32 i_id)
{
    M3Result    result    = m3Err_none;
    u32         numFrames = 0;
    M3Frame*    callFrame = NULL;
    IM3Function function  = NULL;
    m3slot_t*   base;
    u32         numSlots;
    u32         numActivations = 0;
    IM3FuncType expected       = io_cont->entryFunction->funcType;
    m3stack_t   sp;

_   (ContinuationStack(l->runtime, io_cont, &base, &numSlots));

_   (GetLEB_u32(l, &numActivations));
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
        bool                 isInnermost = (a + 1 == numActivations);
        const M3SafePoint*   point;
        M3FrameRegisters     registers;
        u8                   kind       = 0;
        u32                  wasmOffset = 0, targetLoop = 0;
        const M3SnapshotMap* map;

        memset(&registers, 0, sizeof(registers));

_       (GetFunction(l, &function));

        // A return_call hands its frame to the function it calls, so the
        // function standing here need not be the one the continuation was made
        // with, or the one the call outside it named. It does return what
        // they would have.
        _throwif("a frame in the snapshot does not return what its caller expects",
                 not ReturnsWhat(function->funcType, expected));

_       (PrepareFunction(function));

        map = function->snapshotMap;

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

_       (GetU8(l, &kind));
_       (GetLEB_u32(l, &wasmOffset));

        // A back edge says which loop it goes to. That has to be a loop around
        // the instruction; one the instruction does not branch to has no
        // safepoint to match.
        if (kind == safepoint_op) {
            const M3BlockStart* loop;

_           (GetLEB_u32(l, &targetLoop));

            loop = FindBlockAt(map, c_waOp_loop, targetLoop);
            _throwif("a back edge in the snapshot does not go to a loop of its function", not loop);
            _throwif("a back edge in the snapshot goes to a loop that is not around it",
                     not EnclosesOffset(loop, wasmOffset));
        }

        point = FindSafePointAt(map, wasmOffset, kind, targetLoop);
        _throwif("a frame in the snapshot is not at a safepoint of this build's", not point);

_       (AddBlockFrames(l, map, point, sp, &numFrames));

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
            _throwif(m3Err_wasmMalformed, kind != safepoint_op and kind != safepoint_entry and kind != safepoint_suspend);

            io_cont->pc           = point->pc;
            io_cont->suspendPoint = kind;
            registers.r0          = &io_cont->r0;
#  if d_m3HasFloat
            registers.fp0 = &io_cont->fp0;
#  endif
        }

_       (LoadValues(l, base, numSlots, sp, map, point, &registers));

        if (not l->checking) {
            RestoreConstants(function, sp);
        }

        if (not isInnermost) {
            sp = sp + point->aux;
_           (CallSiteType(function, point, &expected));
        } else if (kind == safepoint_resume) {
            void* cont = NULL;
_           (GetRefValue(l, c_m3Type_contref, &cont));
            _throwif(m3Err_wasmMalformed, cont == NULL);

            callFrame->resume.cont = (IM3Continuation)cont;
            if (not l->checking) {
                l->suspendSites[i_id].point = point;
                for (u32 i = 1; i < l->numContinuations; ++i) {
                    if (l->continuations[i] == cont) {
                        // a continuation runs under one resume at a time
                        _throwif("a snapshot continuation is resumed by two frames", l->suspendSites[i].resumed);

                        l->suspendSites[i_id].target = i;
                        l->suspendSites[i].resumed   = true;
                        break;
                    }
                }
            }
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
                if (not l->checking) {
                    l->suspendSites[i_id].map   = map;
                    l->suspendSites[i_id].point = point;
                }

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
    if (l->checking) {
        return m3Err_none;
    }

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
    M3Continuation  scratch;
    if (l->checking) {
        scratch          = *cont;
        scratch.valStack = l->checkStack;
        if (i_id == 0) {
            scratch.valStack = (m3slot_t*)l->runtime->originStack;
        }
        cont = &scratch;
    }

    u32 contId = 0, typeIndex = 0, entryIndex = 0, boundArgsCount = 0;
    u8  state = 0, isRoot = 0;

_   (GetLEB_u32(l, &contId));
_   (GetU8(l, &state));
_   (GetBool(l, &isRoot));
_   (GetLEB_u32(l, &typeIndex));
_   (GetLEB_u32(l, &entryIndex));

    _throwif(m3Err_wasmMalformed, contId != i_id);
    _throwif(m3Err_wasmMalformed, (bool)isRoot != (i_id == 0));
    _throwif(m3Err_wasmMalformed, isRoot and state != snapshot_contSuspended);
    if (not isRoot and (state == snapshot_contAllocated or state == snapshot_contSuspended)) {
        l->loadedStacks++;
    }
    _throwif(m3Err_wasmMalformed, typeIndex != d_m3SnapshotNone and typeIndex >= module->numFuncTypes);
    // every continuation, the root included, was made with a function
    _throwif(m3Err_wasmMalformed, entryIndex >= module->numFunctions);
    _throwif(m3Err_wasmMalformed, isRoot ? typeIndex != d_m3SnapshotNone : (typeIndex == d_m3SnapshotNone or not module->funcTypes[typeIndex]->isContinuation));

    if (not l->checking and not isRoot and state != snapshot_contFinished) {
_       (Continuation_AcquireStack(l->runtime, cont));
    }
    cont->type          = (typeIndex != d_m3SnapshotNone) ? module->funcTypes[typeIndex] : NULL;
    cont->entryFunction = &module->functions[entryIndex];
    cont->numHandlers   = 0;
    cont->handlersPC    = NULL;
    cont->parent        = NULL;
    cont->numFrames     = 0;

#  if d_m3HasExceptionHandling
    {
        void* ref = NULL;
_       (GetRefValue(l, c_m3Type_exnref, &ref));
        cont->resumeThrow = (M3Exception*)ref;
    }
    // the root was not resumed by anything, and a finished continuation has
    // nowhere left to raise an exception
    _throwif(m3Err_wasmMalformed, (isRoot or state == snapshot_contFinished) and cont->resumeThrow != NULL);
#  else
    {
        void* ref = NULL;
_       (GetRefValue(l, c_m3Type_exnref, &ref));
        _throwif(m3Err_wasmMalformed, ref != NULL);
    }
#  endif

    if (state == snapshot_contAllocated) {
        IM3FuncType inner;
        u16         numRets;

_       (GetLEB_u32(l, &boundArgsCount));
        cont->boundArgsCount = boundArgsCount;
        inner                = cont->entryFunction->funcType;

        _throwif(m3Err_wasmMalformed, boundArgsCount > GetFuncTypeNumParams(inner));

        {
            IM3FuncType remaining = cont->type->contFuncType;
            _throwif(m3Err_wasmMalformed, not remaining or
                                            GetFuncTypeNumParams(remaining) != GetFuncTypeNumParams(inner) - boundArgsCount or
                                            GetFuncTypeNumResults(remaining) != GetFuncTypeNumResults(inner));
            for (u16 i = 0; i < GetFuncTypeNumResults(inner); ++i) {
                _throwif(m3Err_wasmMalformed, GetFuncTypeResultType(inner, i) != GetFuncTypeResultType(remaining, i));
            }
            for (u16 i = 0; i < GetFuncTypeNumParams(remaining); ++i) {
                _throwif(m3Err_wasmMalformed, GetFuncTypeParamType(inner, (u16)(boundArgsCount + i)) != GetFuncTypeParamType(remaining, i));
            }
        }

        numRets = GetFuncTypeNumResults(inner);

        _throwif(m3Err_wasmMalformed, (numRets + boundArgsCount) * c_ioSlotCount > cont->numStackSlots);

        for (u32 i = 0; i < boundArgsCount; ++i) {
_           (GetValue(l, GetFuncTypeParamType(inner, (u16)i), cont->valStack + (numRets + i) * c_ioSlotCount));
        }

        cont->state = cont_allocated;
        cont->sp    = cont->valStack;
        cont->pc    = cont->entryFunction->compiled;
    } else if (state == snapshot_contSuspended) {
_       (GetLEB_u32(l, &boundArgsCount));
        _throwif(m3Err_wasmMalformed, isRoot and boundArgsCount != 0);
        cont->boundArgsCount = boundArgsCount;

_       (LoadSuspendedBody(l, cont, i_id));

        cont->state = cont_suspended;
    } else if (state == snapshot_contFinished) {
_       (GetLEB_u32(l, &boundArgsCount));
        _throwif(m3Err_wasmMalformed, boundArgsCount != 0);
        cont->boundArgsCount = 0;
        cont->state          = cont_consumed;
    } else {
        _throw(m3Err_wasmMalformed);
    }

_catch:
    if (result) {
        cont->numFrames = 0;
    }

    return result;
}

static
M3Result LoadContinuations (M3SnapshotLoad* l)
{
    M3Result result = m3Err_none;
    u32      count  = 0;
    l->loadedStacks = 0;

_   (GetLEB_u32(l, &count));
    _throwif(m3Err_wasmMalformed, count == 0 or count != l->numContinuations);

    for (u32 i = 0; i < count; ++i) {
_       (LoadContinuation(l, i));
    }
    _throwif(m3Err_wasmMalformed, l->loadedStacks != l->requiredStacks);

_catch:
    return result;
}

// Cross-record constraints cannot be checked while reading a reference: both
// its target and a nested suspension point may occur later in the stream.
static
M3Result CheckContinuationReferences (M3SnapshotLoad* l)
{
    M3Result result = m3Err_none;

    for (u32 id = 0; id < l->numContinuations; ++id) {
        IM3Continuation cont  = l->continuations[id];
        u32             leaf  = id;
        u32             depth = 0;
#  if d_m3HasTypedRefs
        m3type_t expectedType = l->suspendSites[id].expectedType;
        _throwif("a snapshot continuation reference has the wrong type", expectedType and
                                                                           (not cont->type or not IsSubTypeOf(RefTypeOfFuncType(cont->type, true), expectedType)));
#  endif
        if (l->suspendSites[id].target) {
            // The child may have switched to a different parameter signature
            // after this resume started. Its results must still fit this site.
            IM3FuncType     expected = Module_ContTypeOfRef(l->module, l->suspendSites[id].point->resumeType);
            IM3Continuation child    = l->continuations[l->suspendSites[id].target];
            IM3FuncType     actual   = child->entryFunction ? child->entryFunction->funcType : NULL;
            _throwif(m3Err_wasmMalformed, not expected or not actual);
            expected = expected->contFuncType;
            _throwif(m3Err_wasmMalformed, GetFuncTypeNumResults(actual) != GetFuncTypeNumResults(expected));
            for (u16 i = 0; i < GetFuncTypeNumResults(expected); ++i) {
                _throwif(m3Err_wasmMalformed, GetFuncTypeResultType(actual, i) != GetFuncTypeResultType(expected, i));
            }
        }
        while (l->suspendSites[leaf].target) {
            leaf = l->suspendSites[leaf].target;
            _throwif(m3Err_wasmMalformed, ++depth >= l->numContinuations or
                                            l->continuations[leaf]->state != cont_suspended);
        }
        if (cont->state != cont_suspended or id == 0) {
            continue;
        }

        // A continuation currently executing under a saved resume already
        // received its arguments. Only an independently captured suspension
        // (possibly containing nested resumes) still expects the tag's results.
        if (l->suspendSites[id].resumed) {
            _throwif(m3Err_wasmMalformed, cont->boundArgsCount != 0);
            continue;
        }

        const M3SnapshotSuspendSite* site      = &l->suspendSites[leaf];
        IM3Continuation              inner     = l->continuations[leaf];
        IM3FuncType                  remaining = cont->type->contFuncType;
        _throwif("a captured continuation has no suspension point", not site->point or not remaining);
        _throwif("a captured continuation has the wrong binding count", cont->boundArgsCount > inner->numSuspendResults or
                                                                          GetFuncTypeNumParams(remaining) != inner->numSuspendResults - cont->boundArgsCount);
        _throwif(m3Err_wasmMalformed,
                 GetFuncTypeNumResults(remaining) != GetFuncTypeNumResults(cont->entryFunction->funcType));
        for (u16 i = 0; i < GetFuncTypeNumResults(remaining); ++i) {
            _throwif(m3Err_wasmMalformed, GetFuncTypeResultType(remaining, i) !=
                                            GetFuncTypeResultType(cont->entryFunction->funcType, i));
        }

        for (u32 i = 0; i < inner->numSuspendResults; ++i) {
            const M3SlotValue* value = &site->map->values[site->point->firstValue +
                                                          site->point->numValues - site->point->numResults + i];
            if (i < cont->boundArgsCount) {
                if (IsRefType(value->type) and not IsNullableRef(value->type)) {
                    void* ref = NULL;
                    memcpy(&ref, inner->sp + value->slot, sizeof(ref));
                    _throwif(m3Err_wasmMalformed, not ref);
                }
            } else {
                _throwif("a captured continuation has the wrong parameter type", value->type !=
                                                                                   GetFuncTypeParamType(remaining, (u16)(i - cont->boundArgsCount)));
            }
        }
    }

_catch:
    return result;
}

#  if d_m3HasExceptionHandling

static
M3Result LoadExceptions (M3SnapshotLoad* l)
{
    M3Result result        = m3Err_none;
    u32      numExceptions = 0;

_   (GetLEB_u32(l, &numExceptions));
    _throwif(m3Err_wasmMalformed, numExceptions == 0 or numExceptions != l->numExceptions);

    for (u32 i = 0; i < numExceptions; ++i) {
        u32          exnId = 0, numArgs = 0;
        M3Exception* exn;
        IM3FuncType  type;

_       (GetLEB_u32(l, &exnId));
_       (GetLEB_u32(l, &numArgs));

        _throwif(m3Err_wasmMalformed, exnId != i);

        exn  = l->exceptions[i];
        type = exn->tag->type;

        _throwif(m3Err_wasmMalformed, numArgs != exn->numArgs);

        for (u32 a = 0; a < numArgs; ++a) {
            m3type_t argType = GetFuncTypeParamType(type, (u16)a);
            u64      bits    = 0;
_           (GetValueBits(l, argType, &bits));
            if (not l->checking) {
                exn->args[a] = bits;
            }
        }
    }

_catch:
    return result;
}

#  endif

// Reads the embedder's state through a reader that stops where it ends
typedef struct M3BoundedReader {
    M3SnapshotLoad* load;
    u64             remaining;
    M3Result        error;
} M3BoundedReader;

static
M3Result BoundedReader_Read (void* o_buffer, size_t i_size, void* i_userdata)
{
    M3BoundedReader* br = (M3BoundedReader*)i_userdata;

    if (i_size > br->remaining) {
        br->error = "the host read past the end of its state";
        return br->error;
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
    size_t                 size = (size_t)(l->end - l->pos);     // the whole section, which is never empty

    _throwif("the snapshot carries host state, and nothing here restores it", not hooks->loadHostState);

    if (l->checking) {
        return SkipBytes(l, size);
    }
    reader.load      = l;
    reader.remaining = size;
    reader.error     = m3Err_none;

_   (hooks->loadHostState(hooks->userdata, BoundedReader_Read, &reader, size));
_   (reader.error);
    _throwif("the host did not read all of its state", reader.remaining != 0);

_catch:
    return result;
}

static
M3Result LoadHeader (M3SnapshotLoad* l)
{
    M3Result result = m3Err_none;
    u8       magic[sizeof(c_snapshotMagic)];
    u32      version = 0;

_   (Get(l, magic, sizeof(magic)));
    _throwif(m3Err_wasmMalformed, memcmp(magic, c_snapshotMagic, sizeof(magic)) != 0);

_   (GetLE32(l, &version));
    _throwif(m3Err_wasmMalformed, version != c_snapshotVersion);

_catch:
    return result;
}

static
M3Result LoadMeta (M3SnapshotLoad* l, u32* o_flags)
{
    M3Result result      = m3Err_none;
    u64      timestamp   = 0;
    u64      moduleHash  = 0;
    u64      ownHash     = 0;
    u64      memoryBytes = 0, tableElements = 0;
    u32      activeStacks = 0;
    u64      currentMemory, currentTables;
    SnapshotResourceTotals(l->module, &currentMemory, &currentTables);

_   (GetLEB_u32(l, o_flags));
_   (GetLEB_u64(l, &timestamp));
_   (GetLEB_u64(l, &moduleHash));

    (void)timestamp;

_   (ModuleFingerprint(l->module, &ownHash));
    _throwif("the snapshot was saved from a different module", moduleHash != ownHash);

    // a flag this build does not know changes what the rest of the file means
    _throwif("the snapshot sets a flag this build does not know", *o_flags & ~(u32)d_m3SnapshotFlagPostmortem);

_   (GetLEB_u32(l, &l->numContinuations));
_   (GetLEB_u32(l, &l->numExceptions));
_   (GetLEB_u64(l, &memoryBytes));
_   (GetLEB_u64(l, &tableElements));
_   (GetLEB_u32(l, &activeStacks));
    l->requiredStacks        = activeStacks;
    l->requiredMemoryBytes   = memoryBytes;
    l->requiredTableElements = tableElements;
    _throwif(m3Err_wasmMalformed, memoryBytes < currentMemory or tableElements < currentTables);
    _throwif(m3Err_wasmMalformed, not l->module->numMemories and memoryBytes != 0);
    _throwif(m3Err_wasmMalformed, not l->module->numTables and tableElements != 0);

    // the rest of Meta means the same thing in a postmortem, so it is read
    // whole and the refusal to resume comes after the section, not inside it
    if (*o_flags & d_m3SnapshotFlagPostmortem) {
        for (u32 i = 0; i < l->numExceptions; ++i) {
            u32 tagIndex = 0;
_           (GetLEB_u32(l, &tagIndex));
        }
        return m3Err_none;
    }

    _throwif(m3Err_memoryLimitExceeded, l->runtime->memoryBytesLimit and
                                          memoryBytes - currentMemory > l->runtime->memoryBytesLimit - l->runtime->memoryBytesUsed);
    _throwif(m3Err_tableLimitExceeded, l->runtime->tableElementsLimit and
                                         tableElements - currentTables > l->runtime->tableElementsLimit - l->runtime->tableElementsUsed);
    _throwif(m3Err_continuationLimitExceeded, l->runtime->continuationsLimit and
                                                activeStacks > l->runtime->continuationsLimit - l->runtime->continuationsAllocated);

    _throwif(m3Err_wasmMalformed, l->numContinuations == 0 or activeStacks >= l->numContinuations);
#  if !d_m3HasExceptionHandling
    _throwif("the snapshot holds exceptions, and this build has none", l->numExceptions != 0);
#  endif

    if (l->checking) {
        for (u32 i = 0; i < l->numExceptions; ++i) {
            u32 tagIndex = 0;
_           (GetLEB_u32(l, &tagIndex));
#  if d_m3HasExceptionHandling
            _throwif(m3Err_wasmMalformed, tagIndex >= l->module->numTags);
#  endif
        }
        return m3Err_none;
    }

    l->checkStack = m3_AllocArray(m3slot_t, d_m3ContinuationStackSlots + 4);
    _throwifnull(l->checkStack);
    l->frames = m3_AllocArray(M3Frame, d_m3ContinuationMaxFrames);
    _throwifnull(l->frames);

    l->continuations = m3_AllocArray(IM3Continuation, l->numContinuations);
    _throwifnull(l->continuations);
    l->suspendSites = m3_AllocArray(M3SnapshotSuspendSite, l->numContinuations);
    _throwifnull(l->suspendSites);

    if (not l->runtime->rootContinuation) {
        IM3Continuation root = Continuation_New(l->runtime, NULL, NULL);
        _throwifnull(root);

        // the root borrows the runtime's stack, so it holds none of its own
        root->numStackSlots          = l->runtime->numStackSlots;
        l->runtime->rootContinuation = root;
    }

    l->continuations[0] = l->runtime->rootContinuation;

    for (u32 i = 1; i < l->numContinuations; ++i) {
        l->continuations[i] = Continuation_New(l->runtime, NULL, NULL);
        _throwifnull(l->continuations[i]);
    }

#  if d_m3HasExceptionHandling
    if (l->numExceptions) {
        l->exceptions = m3_AllocArray(M3Exception*, l->numExceptions);
        _throwifnull(l->exceptions);
    }

    for (u32 i = 0; i < l->numExceptions; ++i) {
        u32    tagIndex = 0;
        IM3Tag tag;
        u32    numArgs;

_       (GetLEB_u32(l, &tagIndex));
        _throwif(m3Err_wasmMalformed, tagIndex >= l->module->numTags);

        tag     = TagOfIndex(l->module, tagIndex);
        numArgs = GetFuncTypeNumParams(tag->type);

        l->exceptions[i] = NewException(l->runtime, tag, numArgs);
        _throwifnull(l->exceptions[i]);

        l->exceptions[i]->reified = true;
    }
#  else
    for (u32 i = 0; i < l->numExceptions; ++i) {
        u32 tagIndex = 0;
_       (GetLEB_u32(l, &tagIndex));
    }
#  endif

_catch:
    return result;
}

// Each section is decoded once without changing guest state or calling the host,
// then applied with the same decoder. This checks its complete structure before
// any of its values or opaque host bytes are installed.
static
M3Result LoadSection (M3SnapshotLoad* l, u8 i_sectionId)
{
    M3Result result = m3Err_none;
    u32      flags  = 0;
    switch (i_sectionId) {
    case d_m3SnapshotSection_Meta:
_       (LoadMeta(l, &flags));
        _throwif("postmortem snapshots cannot be resumed", flags & d_m3SnapshotFlagPostmortem);
        l->started = true;
        break;

    case d_m3SnapshotSection_Memory:
_       (LoadMemories(l));
        break;

    case d_m3SnapshotSection_Table:
_       (LoadTables(l));
        break;

    case d_m3SnapshotSection_Global:
_       (LoadGlobals(l));
        break;

    case d_m3SnapshotSection_Segment:
_       (LoadSegments(l));
        break;

#  if d_m3HasExceptionHandling
    case d_m3SnapshotSection_Exception:
_       (LoadExceptions(l));
        break;
#  endif

    case d_m3SnapshotSection_Continuation:
_       (LoadContinuations(l));
        break;

    case d_m3SnapshotSection_HostState:
_       (LoadHostState(l));
        break;

    default:
_       (SkipBytes(l, (size_t)(l->end - l->pos)));
        break;
    }

_catch:
    return result;
}

static
M3Result LoadSections (M3SnapshotLoad* l, bool i_embedded)
{
    M3Result result  = m3Err_none;
    u8*      payload = NULL;

    if (not i_embedded) {
_       (LoadHeader(l));
    }

    while (true) {
        u8  sectionId   = 0;
        u32 sectionSize = 0;

        // the last section ends the file, so the reader running out here is
        // the end of the snapshot rather than a truncated one
        if (GetU8(l, &sectionId)) {
            break;
        }

_       (GetLEB_u32(l, &sectionSize));

        _throwif("the snapshot holds a section twice", l->seenSections[sectionId]);
        l->seenSections[sectionId] = true;
        _throwif("the snapshot holds an empty section", sectionSize == 0);

        // the Meta section says what the runtime has to be made ready for, so
        // nothing may come before it
        _throwif(m3Err_wasmMalformed, not l->started and sectionId != d_m3SnapshotSection_Meta);

        // Read the entire payload before letting a loader or host callback
        // touch the instance. Every body decoder is then bounded by this buffer.
        payload = (u8*)m3_Malloc("snapshot_section", sectionSize);
        _throwifnull(payload);
_       (Get(l, payload, sectionSize));
        l->pos       = payload;
        l->end       = payload + sectionSize;
        l->inSection = true;

        {
            M3SnapshotLoad check = *l;
            check.checking       = true;
_           (LoadSection(&check, sectionId));
            _throwif("a section in the snapshot does not match its declared length", check.pos != check.end);
        }
        l->changed = l->changed or sectionId != d_m3SnapshotSection_Meta;
_       (LoadSection(l, sectionId));

        // a section that reads past its own length has desynced the stream,
        // and one that stops short would leave the next section misread
        _throwif("a section in the snapshot does not match its declared length",
                 l->pos != l->end);
        l->inSection = false;
        m3_Free(payload);
    }

    _throwif("the snapshot has no meta section", not l->started);
    _throwif("the snapshot has no continuation section",
             not l->seenSections[d_m3SnapshotSection_Continuation]);
    _throwif("the snapshot names exceptions it does not hold",
             l->numExceptions and not l->seenSections[d_m3SnapshotSection_Exception]);

    // a store section left out is not "unchanged": it would resume against
    // whatever instantiation happened to leave behind
    _throwif("the snapshot has no memory section",
             l->module->numMemories and not l->seenSections[d_m3SnapshotSection_Memory]);
    _throwif("the snapshot has no table section",
             l->module->numTables and not l->seenSections[d_m3SnapshotSection_Table]);
    _throwif("the snapshot has no global section",
             l->module->numGlobals and not l->seenSections[d_m3SnapshotSection_Global]);
    _throwif("the snapshot has no segment section",
             (l->module->numDataSegments or l->module->numElementSegments) and
               not l->seenSections[d_m3SnapshotSection_Segment]);

_   (CheckContinuationReferences(l));

_catch:
    m3_Free(payload);
    return result;
}

#endif // d_m3HasSnapshots


//---------------------------------------------------------------------------------------------------------------------------------
//  API
//---------------------------------------------------------------------------------------------------------------------------------

#if d_m3HasSnapshots

static
M3Result SaveSnapshot (IM3Runtime io_runtime, M3SnapshotWriter i_writer, void* i_userdata,
                       IM3Module* o_module, bool i_embedded)
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
    save.embedded   = i_embedded;
    save.postmortem = not root or root->state != cont_suspended;

    IM3Function entry = save.postmortem ? io_runtime->entered : root->entryFunction;

    if (o_module) {
        *o_module = NULL;
    }

    // no call has entered anything, so there is no module to describe
    _throwif("there is nothing to snapshot", not entry);

    save.module = entry->module;

    _throwif("a snapshot failed to restore into the module, so it cannot be saved", save.module->isUnusable);

    if (o_module) {
        *o_module = save.module;
    }

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
    return SaveSnapshot(io_runtime, i_writer, i_userdata, NULL, false);
#else
    (void)i_userdata;
    return "snapshots are not available in this build of Wasm3";
#endif
}


static
M3Result LoadSnapshot (IM3Runtime io_runtime, IM3Module i_module, M3SnapshotReader i_reader, void* i_userdata, bool i_embedded)
{
    M3Result result = m3Err_none;
    _throwifnull(io_runtime);
    _throwifnull(i_module);
    _throwifnull(i_reader);

#if d_m3HasSnapshots
    // A snapshot is the whole of the module's state, and is checked against
    // what instantiation made: memories and tables no smaller than they were
    // declared, and nothing the module did since to account for
    _throwif("a snapshot failed to restore into the module, so it cannot be used", i_module->isUnusable);
    _throwif("a snapshot restores only into a freshly instantiated module, and this one has run", i_module->hasRun);

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

        result = LoadSections(&load, i_embedded);

        IM3Continuation root = io_runtime->rootContinuation;

        if (result) {
            if (load.changed) {
                // Part of the snapshot is in the module and part is not: a
                // state the program was never in. It is not resumed, run or
                // saved again; the embedder has to throw it away.
                i_module->isUnusable = true;

                if (root) {
                    root->state     = cont_returned;
                    root->numFrames = 0;
                }
            }
        } else {
            // A suspension is only ever reached with the start function run,
            // or under way in the invocation being restored, so the memories
            // and globals already hold what it did
            i_module->startFunction = -1;
            i_module->hasRun        = true;
        }

        Runtime_PlaceCallStack(io_runtime);

        m3_Free(load.checkStack);
        m3_Free(load.frames);
        m3_Free(load.suspendSites);
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


M3Result m3_LoadSnapshot (IM3Runtime io_runtime, IM3Module i_module, M3SnapshotReader i_reader, void* i_userdata)
{
    return LoadSnapshot(io_runtime, i_module, i_reader, i_userdata, false);
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

#if d_m3HasSnapshots

// Appends one LEB128-encoded u32 to a plain byte cursor
static
u8* EmitLEB_u32 (u8* o_where, u32 i_value)
{
    do {
        u8 byte = i_value & 0x7F;
        i_value >>= 7;
        if (i_value != 0) {
            byte |= 0x80;
        }
        *o_where++ = byte;
    } while (i_value != 0);

    return o_where;
}

static
u32 SizeOfLEB_u32 (u32 i_value)
{
    u32 size = 0;

    do {
        i_value >>= 7;
        ++size;
    } while (i_value != 0);

    return size;
}

// Walks the custom sections of a Wasm binary and finds the one named
// i_secName. o_secStart/o_secEnd frame the whole section, so it can be dropped
// when a new one replaces it; o_data/o_size frame its snapshot payload. Any of
// the four may be NULL. Not finding one is not an error: o_secStart and o_data
// are left NULL.
static
M3Result FindEmbeddedSnapshot (const u8* i_start, const u8* i_end, cstr_t i_secName,
                               const u8** o_secStart, const u8** o_secEnd,
                               const u8** o_data, u32* o_size)
{
    M3Result result = m3Err_none;
    bytes_t  pos    = i_start;
    bool     found  = false;
    cstr_t   name   = NULL;

    if (o_secStart) {
        *o_secStart = NULL;
    }
    if (o_secEnd) {
        *o_secEnd = NULL;
    }
    if (o_data) {
        *o_data = NULL;
    }
    if (o_size) {
        *o_size = 0;
    }

    _throwif(m3Err_wasmMalformed, i_end - pos < 8 or memcmp(pos, "\0asm\1\0\0\0", 8) != 0);
    pos += 8;

    while (pos < i_end) {
        bytes_t secStart  = pos;
        u8      sectionId = *pos++;
        u32     size      = 0;
_       (ReadLEB_u32(&size, &pos, i_end));
        _throwif(m3Err_wasmUnderrun, size > (size_t)(i_end - pos));
        {
            bytes_t payload = pos;
            pos += size;
            if (sectionId == 0) {
_               (Read_utf8(&name, &payload, pos));
                if (strcmp(name, i_secName) == 0) {
                    _throwif("duplicate embedded snapshot name", found);
                    found = true;
                    if (o_secStart) {
                        *o_secStart = secStart;
                    }
                    if (o_secEnd) {
                        *o_secEnd = pos;
                    }
                    if (o_data) {
                        *o_data = payload;
                    }
                    if (o_size) {
                        *o_size = (u32)(pos - payload);
                    }
                }
                m3_Free(name);
            }
        }
    }
_catch:
    m3_Free(name);
    return result;
}

// Builds the custom section name a snapshot of i_name lives under
static
M3Result SnapshotSectionName (cstr_t i_name, char* o_secName, size_t i_size)
{
    if (i_name and *i_name) {
        if (strlen(i_name) > d_m3MaxSnapshotNameLength) {
            return "the snapshot name is too long";
        }
        snprintf(o_secName, i_size, "snapshot.%s", i_name);
    } else {
        snprintf(o_secName, i_size, "snapshot");
    }

    return m3Err_none;
}

// The snapshot of this name the module was parsed with, or NULL. Parsing keeps
// two of the same name, and selecting that name is where they are refused.
static
M3Result ModuleSnapshot (IM3Module i_module, cstr_t i_name, M3EmbeddedSnapshot** o_snapshot)
{
    M3Result result = m3Err_none;
    cstr_t   name   = i_name ? i_name : "";

    *o_snapshot = NULL;

    for (M3EmbeddedSnapshot* snap = i_module->snapshots; snap; snap = snap->next) {
        if (strcmp(snap->name, name) == 0) {
            _throwif("duplicate embedded snapshot name", *o_snapshot);
            *o_snapshot = snap;
        }
    }

_catch:
    return result;
}
#endif

M3Result m3_LoadSnapshotFromBuffer (IM3Runtime io_runtime, IM3Module i_module, const void* i_bytes, size_t i_size)
{
    if (!io_runtime || !i_module || !i_bytes) {
        return m3Err_mallocFailed;
    }
#if d_m3HasSnapshots
    // A Wasm binary carries its snapshot in a custom section. It is taken from
    // the buffer that was handed over, which need not be the one i_module was
    // parsed from - the snapshot still restores into i_module.
    if (i_size >= 8 && memcmp(i_bytes, "\0asm", 4) == 0) {
        const u8* data   = NULL;
        u32       size   = 0;
        M3Result  result = FindEmbeddedSnapshot((const u8*)i_bytes, (const u8*)i_bytes + i_size,
                                                "snapshot", NULL, NULL, &data, &size);
        if (result) {
            return result;
        }
        if (not data) {
            return "the Wasm binary holds no embedded snapshot";
        }

        {
            M3BufferReader br = { data, size, 0 };
            return LoadSnapshot(io_runtime, i_module, BufferReader_Read, &br, true);
        }
    }
    {
        M3BufferReader br = { (const u8*)i_bytes, i_size, 0 };
        return LoadSnapshot(io_runtime, i_module, BufferReader_Read, &br,
                            i_size < 4 or memcmp(i_bytes, c_snapshotMagic, 4) != 0);
    }
#else
    (void)i_size;
    return "snapshots are not available in this build of Wasm3";
#endif
}

bool m3_HasSnapshot (IM3Module i_module, const char* i_name)
{
    if (not i_module) {
        return false;
    }
#if d_m3HasSnapshots
    // two of that name are still one it carries: loading it is what fails
    M3EmbeddedSnapshot* snap = NULL;
    (void)ModuleSnapshot(i_module, i_name, &snap);

    return snap != NULL;
#else
    (void)i_name;
    return false;
#endif
}

M3Result m3_GetEmbeddedSnapshot (IM3Module i_module, const char* i_name,
                                 const void** o_bytes, size_t* o_size)
{
    if (not i_module or not o_bytes or not o_size) {
        return m3Err_mallocFailed;
    }

    *o_bytes = NULL;
    *o_size  = 0;

#if d_m3HasSnapshots
    {
        M3EmbeddedSnapshot* snap   = NULL;
        M3Result            result = ModuleSnapshot(i_module, i_name, &snap);

        if (result) {
            return result;
        }
        if (not snap) {
            return "the module holds no embedded snapshot of that name";
        }

        *o_bytes = snap->data;
        *o_size  = snap->size;

        return m3Err_none;
    }
#else
    (void)i_name;
    return "snapshots are not available in this build of Wasm3";
#endif
}

M3Result m3_LoadEmbeddedSnapshot (IM3Runtime io_runtime, IM3Module i_module, const char* i_name)
{
    if (not io_runtime or not i_module) {
        return m3Err_mallocFailed;
    }
#if d_m3HasSnapshots
    {
        M3EmbeddedSnapshot* snap   = NULL;
        M3Result            result = ModuleSnapshot(i_module, i_name, &snap);
        M3BufferReader      br;

        if (result) {
            return result;
        }
        if (not snap) {
            return "the module holds no embedded snapshot of that name";
        }

        br.buffer = snap->data;
        br.size   = snap->size;
        br.cursor = 0;

        return LoadSnapshot(io_runtime, i_module, BufferReader_Read, &br, true);
    }
#else
    (void)i_name;
    return "snapshots are not available in this build of Wasm3";
#endif
}


M3Result m3_SaveSnapshotToModule (IM3Runtime io_runtime, IM3Module i_module, const char* i_name,
                                  void** o_bytes, size_t* o_size)
{
#if d_m3HasSnapshots
    M3Result       result = m3Err_none;
    M3BufferWriter bw     = { NULL, 0, 0 };
    u8*            out    = NULL;

    char      secName[d_m3MaxSnapshotNameLength + sizeof("snapshot.")];
    u32       nameLen, nameLenLebSize, payloadSize, payloadSizeLebSize;
    size_t    origSize, totalSize;
    const u8* dropStart = NULL;
    const u8* dropEnd   = NULL;
    IM3Module saved     = NULL;
    u8*       p;

    if (not io_runtime or not i_module or not o_bytes or not o_size) {
        return m3Err_mallocFailed;
    }

    *o_bytes = NULL;
    *o_size  = 0;

    _throwif("the module carries no Wasm bytecode to embed a snapshot in",
             not i_module->wasmStart or i_module->wasmEnd <= i_module->wasmStart);

_   (SnapshotSectionName(i_name, secName, sizeof(secName)));

    // An embedded snapshot is there to be resumed - the unnamed one is resumed
    // just by running the module - and a postmortem cannot be. With nothing
    // paused, a save is a postmortem.
    _throwif("a postmortem cannot be embedded in a module",
             not io_runtime->rootContinuation or io_runtime->rootContinuation->state != cont_suspended);

_   (SaveSnapshot(io_runtime, BufferWriter_Write, &bw, &saved, true));

    _throwif("there is nothing to snapshot", not bw.size);

    // the snapshot is of the module the paused call is in: embedding it in any
    // other leaves a binary whose fingerprint can never match
    _throwif("the snapshot belongs to another module", saved != i_module);

    _throwif("the snapshot is too large to embed in a Wasm module", bw.size > 0xF0000000u);

    // a snapshot of this name already in the module is replaced, not added to
_   (FindEmbeddedSnapshot(i_module->wasmStart, i_module->wasmEnd, secName, &dropStart, &dropEnd, NULL, NULL));

    origSize           = (size_t)(i_module->wasmEnd - i_module->wasmStart) - (dropStart ? (size_t)(dropEnd - dropStart) : 0);
    nameLen            = (u32)strlen(secName);
    nameLenLebSize     = SizeOfLEB_u32(nameLen);
    payloadSize        = nameLenLebSize + nameLen + (u32)bw.size;
    payloadSizeLebSize = SizeOfLEB_u32(payloadSize);
    totalSize          = origSize + 1 + payloadSizeLebSize + payloadSize;

    out = (u8*)m3_Malloc("wasm_snapshot_module", totalSize);
    _throwifnull(out);

    p = out;

    if (dropStart) {
        size_t before = (size_t)(dropStart - i_module->wasmStart);
        size_t after  = (size_t)(i_module->wasmEnd - dropEnd);

        memcpy(p, i_module->wasmStart, before);
        p += before;
        memcpy(p, dropEnd, after);
        p += after;
    } else {
        memcpy(p, i_module->wasmStart, origSize);
        p += origSize;
    }

    *p++ = 0x00; // custom section
    p    = EmitLEB_u32(p, payloadSize);
    p    = EmitLEB_u32(p, nameLen);
    memcpy(p, secName, nameLen);
    p += nameLen;
    memcpy(p, bw.buffer, bw.size);

    *o_bytes = out;
    *o_size  = totalSize;
    out      = NULL;

_catch:
    m3_Free(bw.buffer);
    m3_Free(out);

    return result;
#else
    (void)io_runtime;
    (void)i_module;
    (void)i_name;
    (void)o_bytes;
    (void)o_size;
    return "snapshots are not available in this build of Wasm3";
#endif
}
