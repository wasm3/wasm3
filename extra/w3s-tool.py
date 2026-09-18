#!/usr/bin/env python3
"""Read, inspect and rebuild wasm3 snapshots (.w3s).

A snapshot holds the execution state of a suspended runtime: linear memory,
globals, tables, which segments were dropped, and every continuation and
exception the suspended program can still reach - the root continuation that
is the paused call itself among them. `m3_SaveSnapshot` writes it and
`m3_LoadSnapshot` reads it back.

As a command:

    w3s-tool.py info    run.w3s [--wasm run.wasm]
    w3s-tool.py unpack  run.w3s -o run.d [--wasm run.wasm]
    w3s-tool.py pack    run.d -o run.w3s
    w3s-tool.py verify  run.w3s
    w3s-tool.py diff    before.w3s after.w3s

As a module:

    import importlib.util, pathlib
    spec = importlib.util.spec_from_file_location("w3s", "extra/w3s-tool.py")
    w3s = importlib.util.module_from_spec(spec); spec.loader.exec_module(w3s)

    snap = w3s.load("run.w3s")
    print(snap.globals, snap.root.frames)
    snap.memories[0].data          # bytes, expanded from the chunk encoding
    open("out.w3s", "wb").write(w3s.pack(snap))

The format is written in one build's own terms: host byte order, the build's
slot width, and program counters counted in that build's metacode words. The
header says which, and carries a fingerprint of the build and a hash of the
module, so wasm3 refuses a snapshot it cannot resume rather than guessing. No
field is an address: a reference is written as a function index, or as the id
of a continuation or exception the snapshot carries.
"""

import argparse
import json
import os
import struct
import sys

MAGIC = b"W3S"
SUPPORTED_VERSIONS = (1,)

FLAG_POSTMORTEM = 0x1

NONE_INDEX = 0xFFFFFFFF
NULL_REF = 0xFFFFFFFFFFFFFFFF

CHUNK_END = 0x00
CHUNK_RAW = 0x01
CHUNK_FILL_FF = 0x02

# M3ValueType
TYPE_NAMES = {
    0: "none",
    1: "i32",
    2: "i64",
    3: "f32",
    4: "f64",
    5: "v128",
    6: "funcref",
    7: "externref",
    8: "exnref",
    9: "contref",
}
REF_TYPES = (6, 7, 8, 9)

CONT_ALLOCATED = 0
CONT_SUSPENDED = 1
CONT_FINISHED = 2
CONT_STATES = {
    CONT_ALLOCATED: "allocated",
    CONT_SUSPENDED: "suspended",
    CONT_FINISHED: "finished",
}

FRAME_CALL = 0
FRAME_LOOP = 1
FRAME_TRY = 2
FRAME_ENTRY = 3
FRAME_RESUME = 4
FRAME_KINDS = {
    FRAME_CALL: "call",
    FRAME_LOOP: "loop",
    FRAME_TRY: "try",
    FRAME_ENTRY: "entry",
    FRAME_RESUME: "resume",
}

# M3SafePointKind, for what left a continuation where it stopped
SUSPEND_POINTS = {0: "op", 1: "suspend"}


class FormatError(Exception):
    pass


# --------------------------------------------------------------------------- model


class Memory:
    __slots__ = ("_data", "chunks", "has_data", "max_pages", "num_pages", "page_size")

    def __init__(self, num_pages, max_pages, page_size, has_data, chunks):
        self.num_pages = num_pages
        self.max_pages = max_pages
        self.page_size = page_size
        self.has_data = has_data
        self.chunks = chunks  # list of (kind, offset, payload-or-length)
        self._data = None

    @property
    def size(self):
        return self.num_pages * self.page_size

    @property
    def data(self):
        """The linear memory, expanded. Always in Wasm byte order."""
        if self._data is None:
            buf = bytearray(self.size if self.has_data else 0)
            for kind, off, payload in self.chunks:
                if kind == CHUNK_RAW:
                    buf[off : off + len(payload)] = payload
                elif kind == CHUNK_FILL_FF:
                    buf[off : off + payload] = b"\xff" * payload
            self._data = bytes(buf)
        return self._data

    def stats(self):
        raw = sum(len(p) for k, _, p in self.chunks if k == CHUNK_RAW)
        ff = sum(p for k, _, p in self.chunks if k == CHUNK_FILL_FF)
        size = self.size if self.has_data else 0
        return {
            "pages": self.num_pages,
            "max_pages": self.max_pages,
            "page_size": self.page_size,
            "has_data": self.has_data,
            "bytes": size,
            "stored_bytes": raw,
            "filled_ff_bytes": ff,
            "implicit_zero_bytes": size - raw - ff,
            "chunks": len(self.chunks),
        }


class Record:
    """A plain bag of named fields, which is all most of the format is."""

    __slots__ = ()

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))

    def to_dict(self):
        return {k: getattr(self, k) for k in self.__slots__}


class Exception_(Record):
    __slots__ = ("args", "tag_index")


# which fields a frame of each kind carries, past the ones every frame has
FRAME_COMMON_FIELDS = ("kind", "sp_slot", "memory_index", "func_index")
FRAME_KIND_FIELDS = {
    FRAME_CALL: ("pc_offset", "callee_index", "r0_type", "r0", "fp0"),
    FRAME_LOOP: ("pc_offset",),
    FRAME_TRY: ("pc_offset", "num_clauses", "handlers_live"),
    FRAME_ENTRY: ("entry_func_index",),
    FRAME_RESUME: (
        "pc_offset",
        "cont_id",
        "handlers_offset",
        "num_handlers",
        "results_offset",
        "num_results",
    ),
}


class Frame(Record):
    __slots__ = (
        "callee_index",
        "cont_id",
        "entry_func_index",
        "fp0",
        "func_index",
        "handlers_live",
        "handlers_offset",
        "kind",
        "memory_index",
        "num_clauses",
        "num_handlers",
        "num_results",
        "pc_offset",
        "r0",
        "r0_type",
        "results_offset",
        "sp_slot",
    )

    @property
    def kind_name(self):
        return FRAME_KINDS.get(self.kind, f"kind{self.kind}")

    def to_dict(self):
        d = {k: getattr(self, k) for k in FRAME_COMMON_FIELDS}
        d["kind_name"] = self.kind_name
        for k in FRAME_KIND_FIELDS.get(self.kind, ()):
            d[k] = getattr(self, k)
        return d


class Continuation(Record):
    __slots__ = (
        "args",
        "bound_args_count",
        "entry_func_index",
        "fp0",
        "frames",
        "has_own_pc",
        "id",
        "is_root",
        "pc_func_index",
        "pc_offset",
        "r0",
        "r0_type",
        "relocations",
        "resume_throw",
        "sp_slot",
        "stack",
        "state",
        "suspend_point",
        "suspend_results",
        "type_index",
    )

    @property
    def state_name(self):
        return CONT_STATES.get(self.state, f"state{self.state}")


class Snapshot:
    """Everything a .w3s file holds, plus how it was encoded."""

    def __init__(self):
        self.version = 1
        self.flags = 0
        self.endian = "<"
        self.pointer_size = 8
        self.slot_size = 4
        self.build_fingerprint = 0
        self.gas_metered = False
        self.module_hash = 0
        self.exceptions = []
        self.memories = []
        self.globals = []  # list of (type, u64 word)
        self.tables = []  # list of (element type, list of u64 words)
        self.data_dropped = []
        self.elem_dropped = []
        self.continuations = []
        self.names = {}  # func index -> name, if a module was given

    # ---- convenience

    @property
    def endian_name(self):
        return "little" if self.endian == "<" else "big"

    @property
    def is_postmortem(self):
        return bool(self.flags & FLAG_POSTMORTEM)

    @property
    def root(self):
        for c in self.continuations:
            if c.is_root:
                return c
        return None

    def func_name(self, index):
        return self.names.get(index)

    def describe_func(self, index):
        if index is None or index == NONE_INDEX:
            return "-"
        name = self.func_name(index)
        return f"{index} ({name})" if name else f"{index}"

    def describe_value(self, type_id, word):
        if type_id in REF_TYPES:
            if word == NULL_REF:
                return "null"
            if type_id == 6:
                return f"func {self.describe_func(word)}"
            if type_id == 8:
                return f"exception #{word}"
            if type_id == 9:
                return f"continuation #{word}"
            return f"extern {word}"
        return str(_as_signed(word, type_id))

    def slots(self, cont):
        """A continuation's saved value stack as integers. Slots are untyped;
        the relocations say which ones hold references."""
        fmt = "I" if self.slot_size == 4 else "Q"
        count = len(cont.stack) // self.slot_size
        return list(struct.unpack(f"{self.endian}{count}{fmt}", cont.stack))

    def to_dict(self):
        return {
            "format": {
                "magic": "W3S",
                "version": self.version,
                "flags": self.flags,
                "postmortem": self.is_postmortem,
                "endian": self.endian_name,
                "pointer_size": self.pointer_size,
                "slot_size": self.slot_size,
                "build_fingerprint": f"{self.build_fingerprint:016x}",
                "gas_metered": self.gas_metered,
                "module_hash": f"{self.module_hash:016x}",
            },
            "exceptions": [e.to_dict() for e in self.exceptions],
            "memories": [m.stats() for m in self.memories],
            "globals": [
                {
                    "index": i,
                    "type": TYPE_NAMES.get(t, f"type{t}"),
                    "type_id": t,
                    "value": v,
                    "value_signed": _as_signed(v, t),
                }
                for i, (t, v) in enumerate(self.globals)
            ],
            "tables": [
                {
                    "index": i,
                    "type_id": t,
                    "type": TYPE_NAMES.get(t, f"type{t}"),
                    "size": len(elems),
                    "elements": elems,
                    "non_null": sum(1 for e in elems if e != NULL_REF),
                }
                for i, (t, elems) in enumerate(self.tables)
            ],
            "data_dropped": self.data_dropped,
            "elem_dropped": self.elem_dropped,
            "continuations": [_cont_to_dict(self, c) for c in self.continuations],
        }


def _cont_to_dict(snap, c):
    d = {
        "id": c.id,
        "state": c.state_name,
        "state_id": c.state,
        "is_root": c.is_root,
        "type_index": c.type_index,
        "entry_func_index": c.entry_func_index,
        "entry_func_name": snap.func_name(c.entry_func_index),
        "bound_args_count": c.bound_args_count,
        "resume_throw": c.resume_throw,
    }
    if c.args is not None:
        d["args"] = c.args
    if c.stack is not None:
        d.update(
            {
                "saved_slots": len(c.stack) // snap.slot_size,
                "relocations": [
                    {
                        "slot": s,
                        "type_id": t,
                        "type": TYPE_NAMES.get(t, f"type{t}"),
                        "word": w,
                    }
                    for s, t, w in c.relocations
                ],
                "has_own_pc": c.has_own_pc,
                "suspend_point": c.suspend_point,
                "pc_func_index": c.pc_func_index,
                "pc_offset_words": c.pc_offset,
                "sp_slot": c.sp_slot,
                "r0_type": c.r0_type,
                "r0": c.r0,
                "fp0": c.fp0,
                "suspend_results": [
                    {"offset": o, "is64": w} for o, w in c.suspend_results
                ],
                "frames": [
                    dict(f.to_dict(), func_name=snap.func_name(f.func_index))
                    for f in c.frames
                ],
            }
        )
    return d


def _as_signed(value, type_id):
    if type_id == 1:  # i32
        v = value & 0xFFFFFFFF
        return v - (1 << 32) if v >> 31 else v
    if type_id == 2:  # i64
        return value - (1 << 64) if value >> 63 else value
    if type_id == 3:  # f32
        return struct.unpack("<f", struct.pack("<I", value & 0xFFFFFFFF))[0]
    if type_id == 4:  # f64
        return struct.unpack("<d", struct.pack("<Q", value))[0]
    return value


# --------------------------------------------------------------------------- reading


class _Cursor:
    def __init__(self, data, endian):
        self.data = data
        self.pos = 0
        self.endian = endian

    def take(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise FormatError(
                f"truncated: wanted {n} bytes at offset {self.pos}, "
                f"{len(self.data) - self.pos} remain"
            )
        out = self.data[self.pos : self.pos + n]
        self.pos += n
        return out

    def _unpack(self, fmt, size):
        return struct.unpack(f"{self.endian}{fmt}", self.take(size))[0]

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return self._unpack("H", 2)

    def u32(self):
        return self._unpack("I", 4)

    def i32(self):
        return self._unpack("i", 4)

    def u64(self):
        return self._unpack("Q", 8)

    def f64(self):
        return self._unpack("d", 8)

    @property
    def remaining(self):
        return len(self.data) - self.pos


def _sane(count, what, limit=1 << 24):
    if count > limit:
        raise FormatError(f"implausible {what} count: {count}")
    return count


def _read_frame(c):
    f = Frame(kind=c.u8(), sp_slot=c.u32(), memory_index=c.u32(), func_index=c.u32())
    if f.kind == FRAME_CALL:
        f.pc_offset = c.u32()
        f.callee_index = c.u32()
        f.r0_type = c.u8()
        f.r0 = c.u64()
        f.fp0 = c.f64()
    elif f.kind == FRAME_LOOP:
        f.pc_offset = c.u32()
    elif f.kind == FRAME_TRY:
        f.pc_offset = c.u32()
        f.num_clauses = c.u32()
        f.handlers_live = c.u8()
    elif f.kind == FRAME_ENTRY:
        f.entry_func_index = c.u32()
    elif f.kind == FRAME_RESUME:
        f.pc_offset = c.u32()
        f.cont_id = c.u64()
        f.handlers_offset = c.u32()
        f.num_handlers = c.u32()
        f.results_offset = c.u32()
        f.num_results = c.u32()
    else:
        raise FormatError(f"unknown frame kind {f.kind} at offset {c.pos}")
    return f


def _read_continuation(c, snap, cont_id):
    k = Continuation(
        id=cont_id,
        state=c.u8(),
        is_root=bool(c.u8()),
        type_index=c.u32(),
        entry_func_index=c.u32(),
        bound_args_count=c.u32(),
        resume_throw=c.u64(),
    )
    if snap.is_postmortem:
        return k

    if k.state == CONT_ALLOCATED:
        k.args = [
            c.u64()
            for _ in range(_sane(k.bound_args_count, "bound arguments", 1 << 16))
        ]
    elif k.state == CONT_SUSPENDED:
        k.stack = c.take(_sane(c.u32(), "stack slots") * snap.slot_size)
        k.relocations = [
            (c.u32(), c.u8(), c.u64()) for _ in range(_sane(c.u32(), "relocations"))
        ]
        k.has_own_pc = bool(c.u8())
        if k.has_own_pc:
            k.suspend_point = c.u8()
            k.pc_func_index = c.u32()
            k.pc_offset = c.u32()
            k.sp_slot = c.u32()
            k.r0_type = c.u8()
            k.r0 = c.u64()
        k.fp0 = c.f64()
        k.suspend_results = [
            (c.i32(), c.u8()) for _ in range(_sane(c.u32(), "suspend results", 1 << 16))
        ]
        k.frames = [_read_frame(c) for _ in range(_sane(c.u32(), "frames", 1 << 16))]
    elif k.state != CONT_FINISHED:
        raise FormatError(f"continuation {cont_id}: unknown state {k.state}")
    return k


def _parse(data):
    if len(data) < 12:
        raise FormatError("too short to be a snapshot")
    if data[:3] != MAGIC:
        raise FormatError(f"not a W3S snapshot (magic is {data[:3].hex()})")

    s = Snapshot()
    s.version = data[3]
    if s.version not in SUPPORTED_VERSIONS:
        raise FormatError(f"unsupported version {s.version}")

    # the byte order marker follows the flags, and says how to read both
    marker = data[8:10]
    if marker == b"\x02\x01":
        s.endian = "<"
    elif marker == b"\x01\x02":
        s.endian = ">"
    else:
        raise FormatError(f"unrecognized byte order marker {marker.hex()}")

    c = _Cursor(data, s.endian)
    c.take(4)
    s.flags = c.u32()
    c.u16()
    s.pointer_size = c.u8()
    s.slot_size = c.u8()
    if s.slot_size not in (4, 8) or s.pointer_size not in (4, 8):
        raise FormatError(
            f"implausible widths: pointer {s.pointer_size}, slot {s.slot_size}"
        )
    s.build_fingerprint = c.u64()
    s.gas_metered = bool(c.u8())
    s.module_hash = c.u64()

    num_continuations = _sane(c.u32(), "continuations")
    num_exceptions = _sane(c.u32(), "exceptions")

    headers = [(c.u32(), c.u32()) for _ in range(num_exceptions)]

    for _ in range(_sane(c.u32(), "memories")):
        num_pages = c.u64()
        max_pages = c.u64()
        page_size = c.u32()
        has_data = bool(c.u8())
        chunks = []
        if has_data:
            while True:
                kind = c.u8()
                if kind == CHUNK_END:
                    break
                if kind not in (CHUNK_RAW, CHUNK_FILL_FF):
                    raise FormatError(
                        f"unknown memory chunk type 0x{kind:02x} at offset {c.pos - 1}"
                    )
                off, ln = c.u32(), c.u32()
                chunks.append((kind, off, c.take(ln) if kind == CHUNK_RAW else ln))
        s.memories.append(Memory(num_pages, max_pages, page_size, has_data, chunks))

    for _ in range(_sane(c.u32(), "globals")):
        s.globals.append((c.u8(), c.u64()))

    for _ in range(_sane(c.u32(), "tables")):
        type_id = c.u8()
        s.tables.append(
            (type_id, [c.u64() for _ in range(_sane(c.u32(), "table elements"))])
        )

    s.data_dropped = [c.u8() for _ in range(_sane(c.u32(), "data segments"))]
    s.elem_dropped = [c.u8() for _ in range(_sane(c.u32(), "element segments"))]

    for tag_index, num_args in headers:
        s.exceptions.append(
            Exception_(tag_index=tag_index, args=[c.u64() for _ in range(num_args)])
        )

    for i in range(num_continuations):
        s.continuations.append(_read_continuation(c, s, i))

    if c.remaining:
        raise FormatError(f"{c.remaining} trailing bytes")

    return s


def load(source, *, module=None):
    """Read a snapshot from a path, a file object or bytes.

    module is a path to the .wasm the snapshot belongs to, used only to put
    names to function indices.
    """
    if isinstance(source, (bytes, bytearray)):
        data = bytes(source)
    elif hasattr(source, "read"):
        data = source.read()
    else:
        with open(source, "rb") as f:
            data = f.read()

    snap = _parse(data)
    if module:
        snap.names = read_function_names(module)
    return snap


# --------------------------------------------------------------------------- writing


class _Writer:
    def __init__(self, endian):
        self.out = bytearray()
        self.endian = endian

    def _pack(self, fmt, v):
        self.out.extend(struct.pack(f"{self.endian}{fmt}", v))

    def u8(self, v):
        self.out.append(int(v) & 0xFF)

    def u16(self, v):
        self._pack("H", v & 0xFFFF)

    def u32(self, v):
        self._pack("I", v & 0xFFFFFFFF)

    def i32(self, v):
        self._pack("i", v)

    def u64(self, v):
        self._pack("Q", v & 0xFFFFFFFFFFFFFFFF)

    def f64(self, v):
        self._pack("d", v)

    def raw(self, b):
        self.out.extend(b)


def _write_frame(w, f):
    w.u8(f.kind)
    w.u32(f.sp_slot)
    w.u32(f.memory_index)
    w.u32(f.func_index)
    if f.kind == FRAME_CALL:
        w.u32(f.pc_offset)
        w.u32(f.callee_index)
        w.u8(f.r0_type)
        w.u64(f.r0)
        w.f64(f.fp0)
    elif f.kind == FRAME_LOOP:
        w.u32(f.pc_offset)
    elif f.kind == FRAME_TRY:
        w.u32(f.pc_offset)
        w.u32(f.num_clauses)
        w.u8(f.handlers_live)
    elif f.kind == FRAME_ENTRY:
        w.u32(f.entry_func_index)
    elif f.kind == FRAME_RESUME:
        w.u32(f.pc_offset)
        w.u64(f.cont_id)
        w.u32(f.handlers_offset)
        w.u32(f.num_handlers)
        w.u32(f.results_offset)
        w.u32(f.num_results)
    else:
        raise FormatError(f"unknown frame kind {f.kind}")


def pack(snap):
    """Serialize a Snapshot back to bytes, byte-for-byte with what wasm3 wrote."""
    w = _Writer(snap.endian)

    w.raw(MAGIC)
    w.u8(snap.version)
    w.u32(snap.flags)
    w.u16(0x0102)
    w.u8(snap.pointer_size)
    w.u8(snap.slot_size)
    w.u64(snap.build_fingerprint)
    w.u8(snap.gas_metered)
    w.u64(snap.module_hash)

    w.u32(len(snap.continuations))
    w.u32(len(snap.exceptions))

    for e in snap.exceptions:
        w.u32(e.tag_index)
        w.u32(len(e.args))

    w.u32(len(snap.memories))
    for m in snap.memories:
        w.u64(m.num_pages)
        w.u64(m.max_pages)
        w.u32(m.page_size)
        w.u8(m.has_data)
        if m.has_data:
            for kind, off, payload in m.chunks:
                w.u8(kind)
                w.u32(off)
                if kind == CHUNK_RAW:
                    w.u32(len(payload))
                    w.raw(payload)
                else:
                    w.u32(payload)
            w.u8(CHUNK_END)

    w.u32(len(snap.globals))
    for type_id, value in snap.globals:
        w.u8(type_id)
        w.u64(value)

    w.u32(len(snap.tables))
    for type_id, elems in snap.tables:
        w.u8(type_id)
        w.u32(len(elems))
        for e in elems:
            w.u64(e)

    w.u32(len(snap.data_dropped))
    for d in snap.data_dropped:
        w.u8(d)
    w.u32(len(snap.elem_dropped))
    for d in snap.elem_dropped:
        w.u8(d)

    for e in snap.exceptions:
        for a in e.args:
            w.u64(a)

    for k in snap.continuations:
        w.u8(k.state)
        w.u8(k.is_root)
        w.u32(k.type_index)
        w.u32(k.entry_func_index)
        w.u32(k.bound_args_count)
        w.u64(k.resume_throw)

        if snap.is_postmortem:
            continue

        if k.state == CONT_ALLOCATED:
            for a in k.args:
                w.u64(a)
        elif k.state == CONT_SUSPENDED:
            w.u32(len(k.stack) // snap.slot_size)
            w.raw(k.stack)
            w.u32(len(k.relocations))
            for slot, type_id, word in k.relocations:
                w.u32(slot)
                w.u8(type_id)
                w.u64(word)
            w.u8(k.has_own_pc)
            if k.has_own_pc:
                w.u8(k.suspend_point)
                w.u32(k.pc_func_index)
                w.u32(k.pc_offset)
                w.u32(k.sp_slot)
                w.u8(k.r0_type)
                w.u64(k.r0)
            w.f64(k.fp0)
            w.u32(len(k.suspend_results))
            for offset, is64 in k.suspend_results:
                w.i32(offset)
                w.u8(is64)
            w.u32(len(k.frames))
            for f in k.frames:
                _write_frame(w, f)

    return bytes(w.out)


def encode_memory(data, run_threshold=128):
    """Chunk-encode linear memory the way wasm3 does.

    Runs of 0x00 at least run_threshold long are dropped (memory starts zeroed);
    runs of 0xFF that long become a fill. Everything else is stored raw.
    """
    chunks = []
    n = len(data)
    i = 0
    while i < n:
        b = data[i]
        run = 1
        while i + run < n and data[i + run] == b:
            run += 1
        if b in (0x00, 0xFF) and run >= run_threshold:
            if b == 0xFF:
                chunks.append((CHUNK_FILL_FF, i, run))
            i += run
            continue
        start = i
        while i < n:
            b = data[i]
            length = 1
            while i + length < n and data[i + length] == b:
                length += 1
            if b in (0x00, 0xFF) and length >= run_threshold:
                break
            i += length
        chunks.append((CHUNK_RAW, start, data[start:i]))
    return chunks


# --------------------------------------------------------------------------- wasm names


def read_function_names(path):
    """Function index -> name, from a module's name section and its exports.

    The name section is the better source but only covers functions the author
    gave a symbolic name; an exported function may be named nowhere else, so an
    export name stands in where nothing else does. {} if the module cannot be
    read or says nothing.
    """
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return {}

    if len(data) < 8 or data[:4] != b"\x00asm":
        return {}

    pos = 8
    names = {}
    exported = {}

    def leb(p):
        val = shift = 0
        while p < len(data):
            byte = data[p]
            p += 1
            val |= (byte & 0x7F) << shift
            if not byte & 0x80:
                return val, p
            shift += 7
        raise FormatError("truncated LEB128")

    while pos < len(data):
        section_id = data[pos]
        pos += 1
        size, pos = leb(pos)
        end = pos + size
        if end > len(data):
            break

        if section_id == 0:  # custom
            nlen, p = leb(pos)
            if data[p : p + nlen] == b"name":
                p += nlen
                while p < end:
                    sub_id = data[p]
                    p += 1
                    sub_size, p = leb(p)
                    sub_end = p + sub_size
                    if sub_id == 1:  # function names
                        count, q = leb(p)
                        for _ in range(count):
                            idx, q = leb(q)
                            slen, q = leb(q)
                            names[idx] = data[q : q + slen].decode("utf-8", "replace")
                            q += slen
                    p = sub_end

        elif section_id == 7:  # exports
            count, p = leb(pos)
            for _ in range(count):
                nlen, p = leb(p)
                name = data[p : p + nlen].decode("utf-8", "replace")
                p += nlen
                kind = data[p]
                p += 1
                idx, p = leb(p)
                if kind == 0x00:
                    exported.setdefault(idx, name)

        pos = end

    # wasm3 indexes M3Module.functions with imports first, which is the same
    # index space the name and export sections use
    for idx, name in exported.items():
        names.setdefault(idx, name)

    return names


# --------------------------------------------------------------------------- checks


def verify(snap):
    """Structural complaints about a snapshot. Empty list means nothing found."""
    problems = []
    num_conts = len(snap.continuations)
    num_exns = len(snap.exceptions)

    def check_ref(where, type_id, word):
        if type_id not in REF_TYPES or word == NULL_REF:
            return
        if type_id == 7:
            problems.append(f"{where}: an externref, which wasm3 never writes")
        elif type_id == 8 and word >= num_exns:
            problems.append(f"{where}: exception #{word} is not in the snapshot")
        elif type_id == 9 and word >= num_conts:
            problems.append(f"{where}: continuation #{word} is not in the snapshot")

    for i, m in enumerate(snap.memories):
        if m.max_pages and m.num_pages > m.max_pages:
            problems.append(
                f"memory {i}: {m.num_pages} pages exceeds its maximum of {m.max_pages}"
            )
        for kind, off, payload in m.chunks:
            length = len(payload) if kind == CHUNK_RAW else payload
            if off + length > m.size:
                problems.append(
                    f"memory {i}: chunk at {off}+{length} runs past {m.size} bytes"
                )

    for i, (type_id, word) in enumerate(snap.globals):
        if type_id not in TYPE_NAMES:
            problems.append(f"global {i}: unknown type id {type_id}")
        check_ref(f"global {i}", type_id, word)

    for i, (type_id, elems) in enumerate(snap.tables):
        if type_id not in REF_TYPES:
            problems.append(f"table {i}: element type {type_id} is not a reference")
        for j, e in enumerate(elems):
            check_ref(f"table {i}[{j}]", type_id, e)

    roots = [k for k in snap.continuations if k.is_root]
    if snap.is_postmortem:
        if roots:
            problems.append("a postmortem snapshot has a root continuation")
    else:
        if len(roots) != 1 or snap.continuations[0] is not roots[0]:
            problems.append(
                "a resumable snapshot needs exactly one root, as continuation #0"
            )
        elif roots[0].state != CONT_SUSPENDED:
            problems.append("the root continuation is not suspended")

    for k in snap.continuations:
        where = f"continuation #{k.id}"
        if k.resume_throw != NULL_REF and k.resume_throw >= num_exns:
            problems.append(
                f"{where}: resume_throw names exception #{k.resume_throw}, not in the snapshot"
            )
        if k.stack is None:
            continue
        num_slots = len(k.stack) // snap.slot_size
        pointer_slots = -(-snap.pointer_size // snap.slot_size)
        for slot, type_id, word in k.relocations:
            if slot + pointer_slots > num_slots:
                problems.append(
                    f"{where}: a reference at slot {slot} is past the {num_slots} saved"
                )
            if type_id not in REF_TYPES:
                problems.append(
                    f"{where}: relocation at slot {slot} has non-reference type {type_id}"
                )
            check_ref(f"{where} slot {slot}", type_id, word)
        if k.has_own_pc:
            if k.sp_slot > num_slots:
                problems.append(
                    f"{where}: sp is slot {k.sp_slot} but only {num_slots} were saved"
                )
            if k.suspend_point not in SUSPEND_POINTS:
                problems.append(f"{where}: unknown suspend point {k.suspend_point}")
        elif not k.frames or k.frames[-1].kind != FRAME_RESUME:
            problems.append(
                f"{where}: has no pc of its own, but its innermost frame is not a resume"
            )
        for i, f in enumerate(k.frames):
            if f.kind not in FRAME_KINDS:
                problems.append(f"{where} frame {i}: unknown kind {f.kind}")
            if f.sp_slot > num_slots:
                problems.append(
                    f"{where} frame {i}: sp is slot {f.sp_slot} but only {num_slots} were saved"
                )
            if f.kind == FRAME_RESUME:
                if i != len(k.frames) - 1:
                    problems.append(
                        f"{where} frame {i}: a resume that is not the innermost frame"
                    )
                if f.cont_id >= num_conts:
                    problems.append(
                        f"{where} frame {i}: resumes continuation #{f.cont_id}, not in the snapshot"
                    )
            if f.kind == FRAME_CALL and f.r0_type in REF_TYPES:
                check_ref(f"{where} frame {i} register", f.r0_type, f.r0)

    return problems


def diff(a, b):
    """A summary of what changed between two snapshots of the same program."""
    out = {}

    if len(a.memories) == len(b.memories):
        mem = []
        for i, (ma, mb) in enumerate(zip(a.memories, b.memories)):
            da, db = ma.data, mb.data
            if da == db:
                mem.append({"index": i, "changed_bytes": 0, "grew_by_pages": 0})
                continue
            common = min(len(da), len(db))
            changed = sum(1 for j in range(common) if da[j] != db[j])
            first = next((j for j in range(common) if da[j] != db[j]), None)
            mem.append(
                {
                    "index": i,
                    "changed_bytes": changed + abs(len(da) - len(db)),
                    "first_change_at": first,
                    "grew_by_pages": mb.num_pages - ma.num_pages,
                }
            )
        out["memories"] = mem
    else:
        out["memories"] = (
            f"memory count differs: {len(a.memories)} vs {len(b.memories)}"
        )

    out["globals"] = [
        {"index": i, "from": ga[1], "to": gb[1]}
        for i, (ga, gb) in enumerate(zip(a.globals, b.globals))
        if ga[1] != gb[1]
    ]

    ra, rb = a.root, b.root
    out["execution"] = {
        "continuations": [len(a.continuations), len(b.continuations)],
        "exceptions": [len(a.exceptions), len(b.exceptions)],
        "root_pc": [
            (ra.pc_func_index, ra.pc_offset) if ra else None,
            (rb.pc_func_index, rb.pc_offset) if rb else None,
        ],
        "root_frame_depth": [
            len(ra.frames) if ra else None,
            len(rb.frames) if rb else None,
        ],
    }
    return out


# --------------------------------------------------------------------------- unpack / pack


def unpack(snap, directory):
    """Write a snapshot as snapshot.json plus one binary per blob."""
    os.makedirs(directory, exist_ok=True)

    manifest = snap.to_dict()
    manifest["files"] = {}

    for i, m in enumerate(snap.memories):
        if m.has_data:
            name = f"memory{i}.bin"
            with open(os.path.join(directory, name), "wb") as f:
                f.write(m.data)
            manifest["files"][f"memory{i}"] = name

    for k in snap.continuations:
        if k.stack is not None:
            name = f"stack{k.id}.bin"
            with open(os.path.join(directory, name), "wb") as f:
                f.write(k.stack)
            manifest["files"][f"stack{k.id}"] = name

    with open(os.path.join(directory, "snapshot.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=False)
        f.write("\n")

    return manifest


def pack_directory(directory, run_threshold=128):
    """Rebuild a Snapshot from what unpack() wrote."""
    with open(os.path.join(directory, "snapshot.json"), encoding="utf-8") as f:
        manifest = json.load(f)

    snap = Snapshot()
    fmt = manifest["format"]
    snap.version = fmt["version"]
    snap.flags = fmt["flags"]
    snap.endian = "<" if fmt["endian"] == "little" else ">"
    snap.pointer_size = fmt["pointer_size"]
    snap.slot_size = fmt["slot_size"]
    snap.build_fingerprint = int(fmt["build_fingerprint"], 16)
    snap.gas_metered = fmt["gas_metered"]
    snap.module_hash = int(fmt["module_hash"], 16)

    files = manifest.get("files", {})

    def blob(key):
        with open(os.path.join(directory, files[key]), "rb") as f:
            return f.read()

    snap.exceptions = [
        Exception_(tag_index=e["tag_index"], args=e["args"])
        for e in manifest["exceptions"]
    ]

    for i, m in enumerate(manifest["memories"]):
        chunks = (
            encode_memory(blob(f"memory{i}"), run_threshold) if m["has_data"] else []
        )
        snap.memories.append(
            Memory(m["pages"], m["max_pages"], m["page_size"], m["has_data"], chunks)
        )

    snap.globals = [(g["type_id"], g["value"]) for g in manifest["globals"]]
    snap.tables = [(t["type_id"], t["elements"]) for t in manifest["tables"]]
    snap.data_dropped = manifest["data_dropped"]
    snap.elem_dropped = manifest["elem_dropped"]

    for d in manifest["continuations"]:
        k = Continuation(
            id=d["id"],
            state=d["state_id"],
            is_root=d["is_root"],
            type_index=d["type_index"],
            entry_func_index=d["entry_func_index"],
            bound_args_count=d["bound_args_count"],
            resume_throw=d["resume_throw"],
            args=d.get("args"),
        )
        if "relocations" in d:
            k.stack = blob(f"stack{k.id}")
            k.relocations = [
                (r["slot"], r["type_id"], r["word"]) for r in d["relocations"]
            ]
            k.has_own_pc = d["has_own_pc"]
            k.suspend_point = d["suspend_point"]
            k.pc_func_index = d["pc_func_index"]
            k.pc_offset = d["pc_offset_words"]
            k.sp_slot = d["sp_slot"]
            k.r0_type = d["r0_type"]
            k.r0 = d["r0"]
            k.fp0 = d["fp0"]
            k.suspend_results = [(r["offset"], r["is64"]) for r in d["suspend_results"]]
            k.frames = []
            for fd in d["frames"]:
                fr = Frame(**{key: fd.get(key) for key in Frame.__slots__})
                k.frames.append(fr)
        snap.continuations.append(k)

    return snap


# --------------------------------------------------------------------------- CLI


def _human(n):
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024 or unit == "GiB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024.0


def _print_continuation(snap, k):
    kind = "root" if k.is_root else f"type {k.type_index}"
    print(
        f"  #{k.id:<3} {k.state_name:<10} {kind:<9} entry {snap.describe_func(k.entry_func_index)}"
    )
    if k.resume_throw != NULL_REF:
        print(f"         resumes by raising exception #{k.resume_throw}")
    if k.args:
        print(f"         {len(k.args)} bound arguments")
    if k.stack is None:
        return
    print(
        f"         {len(k.stack) // snap.slot_size} slots saved, "
        f"{len(k.relocations)} of them references"
    )
    if k.has_own_pc:
        point = SUSPEND_POINTS.get(k.suspend_point, f"point{k.suspend_point}")
        print(
            f"         stopped ({point}) in {snap.describe_func(k.pc_func_index)} "
            f"at metacode word {k.pc_offset}, sp slot {k.sp_slot}"
        )
    for i, f in enumerate(k.frames):
        extra = ""
        if f.kind == FRAME_CALL:
            extra = f" -> calls {snap.describe_func(f.callee_index)}"
        elif f.kind == FRAME_TRY:
            extra = f" {f.num_clauses} clauses, handlers {'live' if f.handlers_live else 'retired'}"
        elif f.kind == FRAME_RESUME:
            extra = f" -> runs continuation #{f.cont_id}"
        where = f"word {f.pc_offset}" if f.pc_offset is not None else ""
        print(
            f"         {i:2d}  {f.kind_name:<6} in {snap.describe_func(f.func_index):<20} "
            f"{where:<11} sp slot {f.sp_slot}{extra}"
        )


def cmd_info(args):
    snap = load(args.file, module=args.wasm)
    size = os.path.getsize(args.file) if os.path.exists(args.file) else 0

    print(f"{args.file}  {_human(size)}")
    kind = "postmortem" if snap.is_postmortem else "resumable"
    print(f"  format      W3S version {snap.version}, flags 0x{snap.flags:x} ({kind})")
    print(
        f"  encoding    {snap.endian_name}-endian, {snap.pointer_size}-byte pointers, "
        f"{snap.slot_size}-byte slots"
    )
    print(
        f"  build       {snap.build_fingerprint:016x}, gas metering {'on' if snap.gas_metered else 'off'}"
    )
    print(f"  module      {snap.module_hash:016x}")

    if snap.continuations:
        print()
        print(f"  continuations ({len(snap.continuations)}):")
        for k in snap.continuations:
            _print_continuation(snap, k)

    if snap.exceptions:
        print()
        print(f"  exceptions ({len(snap.exceptions)}):")
        for i, e in enumerate(snap.exceptions):
            print(f"  #{i:<3} tag {e.tag_index}, {len(e.args)} args")

    print()
    for i, m in enumerate(snap.memories):
        st = m.stats()
        print(
            f"  memory {i}    {st['pages']} pages ({_human(st['bytes'])}), "
            f"{_human(st['stored_bytes'])} stored in {st['chunks']} chunks, "
            f"{_human(st['implicit_zero_bytes'])} implicit zeroes"
        )

    if snap.globals:
        print(f"  globals     {len(snap.globals)}")
        for i, (type_id, value) in enumerate(snap.globals):
            print(
                f"    {i:2d}  {TYPE_NAMES.get(type_id, f'type{type_id}'):<10} "
                f"{snap.describe_value(type_id, value)}"
            )

    for i, (type_id, elems) in enumerate(snap.tables):
        print(
            f"  table {i}     {TYPE_NAMES.get(type_id, f'type{type_id}')}, {len(elems)} elements, "
            f"{sum(1 for e in elems if e != NULL_REF)} non-null"
        )

    dropped = sum(snap.data_dropped) + sum(snap.elem_dropped)
    if snap.data_dropped or snap.elem_dropped:
        print(
            f"  segments    {len(snap.data_dropped)} data, {len(snap.elem_dropped)} element, "
            f"{dropped} dropped"
        )

    problems = verify(snap)
    if problems:
        print()
        print("  problems:")
        for p in problems:
            print(f"    - {p}")

    return 1 if problems else 0


def cmd_unpack(args):
    snap = load(args.file, module=args.wasm)
    manifest = unpack(snap, args.output)
    print(f"unpacked to {args.output}")
    for key, name in manifest["files"].items():
        path = os.path.join(args.output, name)
        print(f"  {key:<10} {name} ({_human(os.path.getsize(path))})")
    print(f"  {'manifest':<10} snapshot.json")
    return 0


def cmd_pack(args):
    snap = pack_directory(args.directory, args.run_threshold)
    data = pack(snap)
    with open(args.output, "wb") as f:
        f.write(data)
    print(f"wrote {args.output} ({_human(len(data))})")
    return 0


def cmd_verify(args):
    snap = load(args.file, module=args.wasm)
    problems = verify(snap)
    if not problems:
        print(f"{args.file}: ok")
        return 0
    for p in problems:
        print(f"{args.file}: {p}")
    return 1


def cmd_diff(args):
    a = load(args.before, module=args.wasm)
    b = load(args.after, module=args.wasm)
    print(json.dumps(diff(a, b), indent=2))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Read, inspect and rebuild wasm3 snapshots (.w3s)"
    )
    parser.add_argument(
        "--wasm", help="the module the snapshot belongs to, for function names"
    )
    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("info", help="summarize a snapshot")
    p.add_argument("file")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("unpack", help="write JSON plus one binary per blob")
    p.add_argument("file")
    p.add_argument("-o", "--output", required=True, help="directory to write to")
    p.set_defaults(func=cmd_unpack)

    p = sub.add_parser("pack", help="rebuild a snapshot from an unpacked directory")
    p.add_argument("directory")
    p.add_argument("-o", "--output", required=True)
    p.add_argument(
        "--run-threshold",
        type=int,
        default=128,
        help="run length that triggers sparse encoding (default 128)",
    )
    p.set_defaults(func=cmd_pack)

    p = sub.add_parser("verify", help="check a snapshot for structural damage")
    p.add_argument("file")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("diff", help="compare two snapshots of the same program")
    p.add_argument("before")
    p.add_argument("after")
    p.set_defaults(func=cmd_diff)

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 2

    try:
        return args.func(args)
    except FormatError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
