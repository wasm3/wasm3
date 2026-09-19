#!/usr/bin/env python3
"""Read, inspect and rebuild wasm3 snapshots (.w3s).

A snapshot holds the execution state of a suspended runtime: linear memory,
globals, tables, which segments were dropped, every continuation and exception
the suspended program can still reach - the root continuation that is the
paused call itself among them - and whatever state the embedder saved with it.
`m3_SaveSnapshot` writes it and `m3_LoadSnapshot` reads it back.

As a command:

    w3s-tool.py info    run.w3s [--wasm run.wasm]
    w3s-tool.py unpack  run.w3s -o run.d [--wasm run.wasm]
    w3s-tool.py pack    run.d -o run.w3s
    w3s-tool.py verify  run.w3s [--wasm run.wasm]
    w3s-tool.py diff    before.w3s after.w3s

As a module:

    import importlib.util, pathlib
    spec = importlib.util.spec_from_file_location("w3s", "extra/w3s-tool.py")
    w3s = importlib.util.module_from_spec(spec); spec.loader.exec_module(w3s)

    snap = w3s.load("run.w3s")
    print(snap.globals, snap.root.activations)
    snap.memories[0].data          # bytes, expanded from the chunk encoding
    open("out.w3s", "wb").write(w3s.pack(snap))

The format is written in Wasm's terms, not in any one build's. Every number is
little endian. A place in a function is a byte offset into its body, and a
suspended function is its locals and live operand stack as typed values, so a
snapshot can be resumed by a build of another architecture, slot width or
revision. No field is an address: a reference is written as a function index,
as the id of a continuation or exception the snapshot carries, or as the name
the embedder gave an externref.
"""

import argparse
import json
import os
import struct
import sys

MAGIC = b"W3S"
SUPPORTED_VERSIONS = (1,)
HEADER_SIZE = 40

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
VALUE_TYPES = (1, 2, 3, 4, 6, 7, 8, 9)
REF_TYPES = (6, 7, 8, 9)

CONT_ALLOCATED = 0
CONT_SUSPENDED = 1
CONT_FINISHED = 2
CONT_STATES = {
    CONT_ALLOCATED: "allocated",
    CONT_SUSPENDED: "suspended",
    CONT_FINISHED: "finished",
}

BLOCK_LOOP = 1
BLOCK_TRY = 2
BLOCK_KINDS = {BLOCK_LOOP: "loop", BLOCK_TRY: "try_table"}

# M3SafePointKind: where a function's frame can be left standing
SITE_OP = 0
SITE_SUSPEND = 1
SITE_CALL = 2
SITE_RESUME = 3
SITE_GAS = 4
SITE_KINDS = {
    SITE_OP: "back edge",
    SITE_SUSPEND: "suspend",
    SITE_CALL: "call",
    SITE_RESUME: "resume",
    SITE_GAS: "gas charge",
}
# what the innermost function of a continuation can have stopped at
INNERMOST_SITES = (SITE_OP, SITE_SUSPEND, SITE_RESUME, SITE_GAS)


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
        """The linear memory, expanded."""
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


class Block(Record):
    """A loop or try_table standing in a function's frame, by its offset."""

    __slots__ = ("handlers_live", "kind", "wasm_offset")

    @property
    def kind_name(self):
        return BLOCK_KINDS.get(self.kind, f"kind{self.kind}")

    def to_dict(self):
        d = {
            "kind": self.kind_name,
            "kind_id": self.kind,
            "wasm_offset": self.wasm_offset,
        }
        if self.kind == BLOCK_TRY:
            d["handlers_live"] = self.handlers_live
        return d


class Activation(Record):
    """One function's frame in a suspended continuation: where it stands and
    what it holds there - its locals, then its live operand stack."""

    __slots__ = (
        "blocks",
        "cont_id",
        "func_index",
        "ordinal",
        "site",
        "values",
        "wasm_offset",
    )

    @property
    def site_name(self):
        return SITE_KINDS.get(self.site, f"site{self.site}")


class Continuation(Record):
    __slots__ = (
        "activations",
        "args",
        "bound_args_count",
        "entry_func_index",
        "id",
        "is_root",
        "resume_throw",
        "state",
        "type_index",
    )

    @property
    def state_name(self):
        return CONT_STATES.get(self.state, f"state{self.state}")


class Snapshot:
    """Everything a .w3s file holds."""

    def __init__(self):
        self.version = 1
        self.flags = 0
        self.timestamp_ms = 0
        self.wasm3_hash = 0
        self.module_hash = 0
        self.exceptions = []
        self.memories = []
        self.globals = []  # list of (type, u64 word)
        self.tables = []  # list of (element type, list of u64 words)
        self.data_dropped = []
        self.elem_dropped = []
        self.continuations = []
        self.host_state = b""
        self.module = None  # ModuleInfo, if a module was given

    # ---- convenience

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
        return self.module.names.get(index) if self.module else None

    def describe_func(self, index):
        if index is None or index == NONE_INDEX:
            return "-"
        name = self.func_name(index)
        return f"{index} ({name})" if name else f"{index}"

    def describe_offset(self, func_index, wasm_offset):
        """An offset in a body, and in the module if the module is known -
        which is what wasm-objdump -d prints."""
        absolute = (
            self.module.absolute_offset(func_index, wasm_offset)
            if self.module
            else None
        )
        if absolute is None:
            return f"+0x{wasm_offset:x}"
        return f"+0x{wasm_offset:x} (0x{absolute:x})"

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

    def num_locals(self, func_index):
        """How many of a function's values are its locals, arguments included,
        if the module is known."""
        return self.module.num_locals(func_index) if self.module else None

    def to_dict(self):
        return {
            "format": {
                "magic": "W3S",
                "version": self.version,
                "flags": self.flags,
                "postmortem": self.is_postmortem,
                "timestamp_ms": self.timestamp_ms,
                "wasm3_hash": f"{self.wasm3_hash:016x}",
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
            "host_state_bytes": len(self.host_state),
        }


def _activation_to_dict(snap, a):
    d = {
        "func_index": a.func_index,
        "func_name": snap.func_name(a.func_index),
        "blocks": [b.to_dict() for b in a.blocks],
        "site": a.site_name,
        "site_id": a.site,
        "wasm_offset": a.wasm_offset,
        "ordinal": a.ordinal,
        "values": [
            {"type": TYPE_NAMES.get(t, f"type{t}"), "type_id": t, "word": w}
            for t, w in a.values
        ],
    }
    num_locals = snap.num_locals(a.func_index)
    if num_locals is not None:
        d["num_locals"] = num_locals
    if a.site == SITE_RESUME:
        d["cont_id"] = a.cont_id
    return d


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
    if c.activations is not None:
        d["activations"] = [_activation_to_dict(snap, a) for a in c.activations]
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
    def __init__(self, data):
        self.data = data
        self.pos = 0

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
        return struct.unpack(f"<{fmt}", self.take(size))[0]

    def u8(self):
        return self.take(1)[0]

    def u32(self):
        return self._unpack("I", 4)

    def u64(self):
        return self._unpack("Q", 8)

    @property
    def remaining(self):
        return len(self.data) - self.pos


def _sane(count, what, limit=1 << 24):
    if count > limit:
        raise FormatError(f"implausible {what} count: {count}")
    return count


def _read_activation(c):
    a = Activation(func_index=c.u32(), blocks=[])
    for _ in range(_sane(c.u32(), "blocks", 1 << 16)):
        b = Block(kind=c.u8(), wasm_offset=c.u32())
        if b.kind == BLOCK_TRY:
            b.handlers_live = c.u8()
        elif b.kind != BLOCK_LOOP:
            raise FormatError(f"unknown block kind {b.kind} at offset {c.pos - 5}")
        a.blocks.append(b)
    a.site = c.u8()
    a.wasm_offset = c.u32()
    a.ordinal = c.u32()
    a.values = [(c.u8(), c.u64()) for _ in range(_sane(c.u32(), "values", 1 << 20))]
    if a.site == SITE_RESUME:
        a.cont_id = c.u64()
    elif a.site not in SITE_KINDS:
        raise FormatError(f"unknown safepoint kind {a.site} at offset {c.pos}")
    return a


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
        count = _sane(c.u32(), "activations", 1 << 16)
        k.activations = []
        for _ in range(count):
            a = _read_activation(c)
            k.activations.append(a)
            # a resume ends the list: the rest is the continuation it runs
            if a.site in INNERMOST_SITES and len(k.activations) != count:
                raise FormatError(
                    f"continuation {cont_id}: a function stopped at a {a.site_name} "
                    f"is not the innermost"
                )
    elif k.state != CONT_FINISHED:
        raise FormatError(f"continuation {cont_id}: unknown state {k.state}")
    return k


def _parse(data):
    if len(data) < HEADER_SIZE:
        raise FormatError("too short to be a snapshot")
    if data[:3] != MAGIC:
        raise FormatError(f"not a W3S snapshot (magic is {data[:3].hex()})")

    s = Snapshot()
    s.version = data[3]
    if s.version not in SUPPORTED_VERSIONS:
        raise FormatError(f"unsupported version {s.version}")

    c = _Cursor(data)
    c.take(4)
    s.flags = c.u32()
    s.timestamp_ms = c.u64()
    s.wasm3_hash = c.u64()
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

    s.host_state = c.take(_sane(c.u64(), "host state bytes", 1 << 40))

    if c.remaining:
        raise FormatError(f"{c.remaining} trailing bytes")

    return s


def load(source, *, module=None):
    """Read a snapshot from a path, a file object or bytes.

    module is a path to the .wasm the snapshot belongs to, used to put names to
    function indices, to tell a function's locals from its operand stack, and
    to turn offsets in a body into offsets in the module.
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
        snap.module = ModuleInfo.read(module)
    return snap


# --------------------------------------------------------------------------- writing


class _Writer:
    def __init__(self):
        self.out = bytearray()

    def _pack(self, fmt, v):
        self.out.extend(struct.pack(f"<{fmt}", v))

    def u8(self, v):
        self.out.append(int(v) & 0xFF)

    def u32(self, v):
        self._pack("I", v & 0xFFFFFFFF)

    def u64(self, v):
        self._pack("Q", v & 0xFFFFFFFFFFFFFFFF)

    def raw(self, b):
        self.out.extend(b)


def _write_activation(w, a):
    w.u32(a.func_index)
    w.u32(len(a.blocks))
    for b in a.blocks:
        w.u8(b.kind)
        w.u32(b.wasm_offset)
        if b.kind == BLOCK_TRY:
            w.u8(b.handlers_live)
    w.u8(a.site)
    w.u32(a.wasm_offset)
    w.u32(a.ordinal)
    w.u32(len(a.values))
    for type_id, word in a.values:
        w.u8(type_id)
        w.u64(word)
    if a.site == SITE_RESUME:
        w.u64(a.cont_id)


def pack(snap):
    """Serialize a Snapshot back to bytes, byte-for-byte with what wasm3 wrote."""
    w = _Writer()

    w.raw(MAGIC)
    w.u8(snap.version)
    w.u32(snap.flags)
    w.u64(snap.timestamp_ms)
    w.u64(snap.wasm3_hash)
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
            w.u32(len(k.activations))
            for a in k.activations:
                _write_activation(w, a)

    w.u64(len(snap.host_state))
    w.raw(snap.host_state)

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


# --------------------------------------------------------------------------- the module


class ModuleInfo:
    """What a snapshot needs from its module to be read in Wasm's terms:
    function names, how many locals each function has, and where each body
    starts in the module.

    Function indices count imports first, as wasm3 and the binary format both
    do. An imported function has no body.
    """

    def __init__(self):
        self.names = {}
        self.num_imported = 0
        self.func_types = []  # type index per function, imports first
        self.type_params = {}  # type index -> param count, for function types
        self.bodies = (
            []
        )  # (start of the body after its size, declared locals), per defined function

    def num_locals(self, func_index):
        defined = func_index - self.num_imported
        if func_index < self.num_imported or defined >= len(self.bodies):
            return None
        params = self.type_params.get(self.func_types[func_index])
        if params is None:
            return None
        return params + self.bodies[defined][1]

    def absolute_offset(self, func_index, wasm_offset):
        defined = func_index - self.num_imported
        if func_index < self.num_imported or defined >= len(self.bodies):
            return None
        return self.bodies[defined][0] + wasm_offset

    @classmethod
    def read(cls, path):
        """Whatever the module says, or as much of it as reads cleanly."""
        info = cls()
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            return info
        if len(data) < 8 or data[:4] != b"\x00asm":
            return info
        try:
            info._parse(data)
        except (FormatError, IndexError):
            pass
        return info

    def _parse(self, data):
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

        def value_type(p):
            # a reference type spelled out is a prefix and then its heap type
            if data[p] in (0x63, 0x64):
                return leb(p + 1)[1]
            return p + 1

        def limits(p):
            flags = data[p]
            _, p = leb(p + 1)
            if flags & 0x01:
                _, p = leb(p)
            if flags & 0x08:  # a custom page size
                _, p = leb(p)
            return p

        def comp_type(p, type_index):
            form = data[p]
            if form == 0x60:  # func
                count, p = leb(p + 1)
                self.type_params[type_index] = count
                for _ in range(count):
                    p = value_type(p)
                count, p = leb(p)
                for _ in range(count):
                    p = value_type(p)
                return p
            if form == 0x5D:  # cont
                return leb(p + 1)[1]
            raise FormatError(f"type form 0x{form:02x} is not one this reads")

        exported = {}
        pos = 8
        while pos < len(data):
            section_id = data[pos]
            size, pos = leb(pos + 1)
            end = pos + size
            if end > len(data):
                break

            if section_id == 0:  # custom
                nlen, p = leb(pos)
                if data[p : p + nlen] == b"name":
                    p += nlen
                    while p < end:
                        sub_id = data[p]
                        sub_size, p = leb(p + 1)
                        sub_end = p + sub_size
                        if sub_id == 1:  # function names
                            count, q = leb(p)
                            for _ in range(count):
                                idx, q = leb(q)
                                slen, q = leb(q)
                                self.names[idx] = data[q : q + slen].decode(
                                    "utf-8", "replace"
                                )
                                q += slen
                        p = sub_end

            elif section_id == 1:  # types
                count, p = leb(pos)
                index = 0
                for _ in range(count):
                    if data[p] == 0x4E:  # a recursive group
                        members, p = leb(p + 1)
                    else:
                        members = 1
                    for _ in range(members):
                        if data[p] in (0x50, 0x4F):  # sub, sub final
                            supers, p = leb(p + 1)
                            for _ in range(supers):
                                _, p = leb(p)
                        p = comp_type(p, index)
                        index += 1

            elif section_id == 2:  # imports
                count, p = leb(pos)
                for _ in range(count):
                    for _ in range(2):
                        slen, p = leb(p)
                        p += slen
                    kind = data[p]
                    p += 1
                    if kind == 0x00:  # function
                        type_index, p = leb(p)
                        self.func_types.append(type_index)
                        self.num_imported += 1
                    elif kind == 0x01:  # table
                        p = limits(value_type(p))
                    elif kind == 0x02:  # memory
                        p = limits(p)
                    elif kind == 0x03:  # global
                        p = value_type(p) + 1
                    elif kind == 0x04:  # tag
                        p = leb(p + 1)[1]
                    else:
                        raise FormatError(f"import kind 0x{kind:02x}")

            elif section_id == 3:  # functions
                count, p = leb(pos)
                for _ in range(count):
                    type_index, p = leb(p)
                    self.func_types.append(type_index)

            elif section_id == 7:  # exports
                count, p = leb(pos)
                for _ in range(count):
                    nlen, p = leb(p)
                    name = data[p : p + nlen].decode("utf-8", "replace")
                    p += nlen
                    kind = data[p]
                    idx, p = leb(p + 1)
                    if kind == 0x00:
                        exported.setdefault(idx, name)

            elif section_id == 10:  # code
                count, p = leb(pos)
                for _ in range(count):
                    body_size, p = leb(p)
                    body_end = p + body_size
                    groups, q = leb(p)
                    declared = 0
                    for _ in range(groups):
                        n, q = leb(q)
                        declared += n
                        q = value_type(q)
                    self.bodies.append((p, declared))
                    p = body_end

            pos = end

        # an exported function may be named nowhere else
        for idx, name in exported.items():
            self.names.setdefault(idx, name)


def read_function_names(path):
    """Function index -> name, from a module's name section and its exports."""
    return ModuleInfo.read(path).names


# --------------------------------------------------------------------------- checks


def verify(snap):
    """Structural complaints about a snapshot. Empty list means nothing found."""
    problems = []
    num_conts = len(snap.continuations)
    num_exns = len(snap.exceptions)

    def check_ref(where, type_id, word):
        if type_id not in REF_TYPES or word == NULL_REF:
            return
        if type_id == 8 and word >= num_exns:
            problems.append(f"{where}: exception #{word} is not in the snapshot")
        elif type_id == 9 and word >= num_conts:
            problems.append(f"{where}: continuation #{word} is not in the snapshot")
        elif type_id == 6 and snap.module and word >= len(snap.module.func_types):
            problems.append(f"{where}: function {word} is not in the module")

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
        if type_id not in VALUE_TYPES:
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
        if snap.host_state:
            problems.append("a postmortem snapshot carries host state")
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
        if k.activations is None:
            continue
        if not k.activations:
            problems.append(f"{where}: suspended, but in no function")
            continue
        if k.activations[0].func_index != k.entry_func_index:
            problems.append(
                f"{where}: the outermost function is not the one it was entered with"
            )
        for i, a in enumerate(k.activations):
            at = f"{where} function {i}"
            innermost = i == len(k.activations) - 1
            if innermost and a.site not in INNERMOST_SITES:
                problems.append(
                    f"{at}: the innermost function waits at a {a.site_name}"
                )
            if not innermost and a.site != SITE_CALL:
                problems.append(
                    f"{at}: stopped at a {a.site_name}, but is not the innermost"
                )
            if a.site == SITE_RESUME and a.cont_id >= num_conts:
                problems.append(
                    f"{at}: resumes continuation #{a.cont_id}, not in the snapshot"
                )
            for b in a.blocks:
                if b.kind not in BLOCK_KINDS:
                    problems.append(f"{at}: unknown block kind {b.kind}")
            num_locals = snap.num_locals(a.func_index)
            if num_locals is not None and len(a.values) < num_locals:
                problems.append(
                    f"{at}: {len(a.values)} values, fewer than its {num_locals} locals"
                )
            for j, (type_id, word) in enumerate(a.values):
                if type_id not in VALUE_TYPES:
                    problems.append(f"{at} value {j}: unknown type id {type_id}")
                elif type_id in (1, 3) and word >> 32:
                    problems.append(
                        f"{at} value {j}: a 32-bit value with high bits set"
                    )
                check_ref(f"{at} value {j}", type_id, word)

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

    def where(snap):
        root = snap.root
        if not root or not root.activations:
            return None
        inner = root.activations[-1]
        return {
            "func_index": inner.func_index,
            "site": inner.site_name,
            "wasm_offset": inner.wasm_offset,
        }

    ra, rb = a.root, b.root
    out["execution"] = {
        "continuations": [len(a.continuations), len(b.continuations)],
        "exceptions": [len(a.exceptions), len(b.exceptions)],
        "root_stopped_at": [where(a), where(b)],
        "root_call_depth": [
            len(ra.activations) if ra and ra.activations else None,
            len(rb.activations) if rb and rb.activations else None,
        ],
        "elapsed_ms": (
            b.timestamp_ms - a.timestamp_ms
            if a.timestamp_ms and b.timestamp_ms
            else None
        ),
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

    if snap.host_state:
        with open(os.path.join(directory, "host.bin"), "wb") as f:
            f.write(snap.host_state)
        manifest["files"]["host"] = "host.bin"

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
    snap.timestamp_ms = fmt["timestamp_ms"]
    snap.wasm3_hash = int(fmt["wasm3_hash"], 16)
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
        if "activations" in d:
            k.activations = [
                Activation(
                    func_index=ad["func_index"],
                    blocks=[
                        Block(
                            kind=bd["kind_id"],
                            wasm_offset=bd["wasm_offset"],
                            handlers_live=bd.get("handlers_live"),
                        )
                        for bd in ad["blocks"]
                    ],
                    site=ad["site_id"],
                    wasm_offset=ad["wasm_offset"],
                    ordinal=ad["ordinal"],
                    values=[(v["type_id"], v["word"]) for v in ad["values"]],
                    cont_id=ad.get("cont_id"),
                )
                for ad in d["activations"]
            ]
        snap.continuations.append(k)

    snap.host_state = blob("host") if "host" in files else b""

    return snap


# --------------------------------------------------------------------------- CLI


def _human(n):
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024 or unit == "GiB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024.0


def _print_values(snap, a, indent):
    num_locals = snap.num_locals(a.func_index)
    for j, (type_id, word) in enumerate(a.values):
        if num_locals is None:
            label = f"[{j}]"
        elif j < num_locals:
            label = f"local {j}"
        else:
            label = f"stack {j - num_locals}"
        print(
            f"{indent}{label:<10} {TYPE_NAMES.get(type_id, f'type{type_id}'):<10}"
            f"{snap.describe_value(type_id, word)}"
        )


def _print_continuation(snap, k, verbose):
    kind = "root" if k.is_root else f"type {k.type_index}"
    print(
        f"  #{k.id:<3} {k.state_name:<10} {kind:<9} entry {snap.describe_func(k.entry_func_index)}"
    )
    if k.resume_throw != NULL_REF:
        print(f"         resumes by raising exception #{k.resume_throw}")
    if k.args:
        print(f"         {len(k.args)} bound arguments")
    if not k.activations:
        return
    for i, a in enumerate(k.activations):
        where = snap.describe_offset(a.func_index, a.wasm_offset)
        blocks = " ".join(
            f"{b.kind_name}@{snap.describe_offset(a.func_index, b.wasm_offset)}"
            + ("" if b.kind != BLOCK_TRY or b.handlers_live else " (retired)")
            for b in a.blocks
        )
        extra = f" -> runs continuation #{a.cont_id}" if a.site == SITE_RESUME else ""
        print(
            f"         {i:2d}  {snap.describe_func(a.func_index):<20} "
            f"at {a.site_name} {where}{extra}, {len(a.values)} values"
        )
        if blocks:
            print(f"             inside {blocks}")
        if verbose:
            _print_values(snap, a, "             ")


def cmd_info(args):
    snap = load(args.file, module=args.wasm)
    size = os.path.getsize(args.file) if os.path.exists(args.file) else 0

    print(f"{args.file}  {_human(size)}")
    kind = "postmortem" if snap.is_postmortem else "resumable"
    print(f"  format      W3S version {snap.version}, flags 0x{snap.flags:x} ({kind})")
    if snap.timestamp_ms:
        import datetime

        when = datetime.datetime.fromtimestamp(
            snap.timestamp_ms / 1000.0, tz=datetime.timezone.utc
        )
        print(f"  taken       {when.isoformat(timespec='milliseconds')}")
    print(f"  wasm3       {snap.wasm3_hash:016x}")
    print(f"  module      {snap.module_hash:016x}")

    if snap.continuations:
        print()
        print(f"  continuations ({len(snap.continuations)}):")
        for k in snap.continuations:
            _print_continuation(snap, k, args.values)

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

    if snap.host_state:
        print(f"  host state  {_human(len(snap.host_state))}")

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
    with open(args.file, "rb") as f:
        data = f.read()
    snap = load(data, module=args.wasm)
    problems = verify(snap)
    if pack(snap) != data:
        problems.append("does not pack back into the same bytes")
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
    wasm_help = (
        "the module the snapshot belongs to: function names, locals, module offsets"
    )

    parser = argparse.ArgumentParser(
        description="Read, inspect and rebuild wasm3 snapshots (.w3s)"
    )
    parser.add_argument("--wasm", help=wasm_help)

    # also taken after the command, where it does not overwrite one given before it
    module = argparse.ArgumentParser(add_help=False)
    module.add_argument("--wasm", default=argparse.SUPPRESS, help=wasm_help)

    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("info", parents=[module], help="summarize a snapshot")
    p.add_argument("file")
    p.add_argument(
        "-v", "--values", action="store_true", help="list every value each frame holds"
    )
    p.set_defaults(func=cmd_info)

    p = sub.add_parser(
        "unpack", parents=[module], help="write JSON plus one binary per blob"
    )
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

    p = sub.add_parser(
        "verify", parents=[module], help="check a snapshot for structural damage"
    )
    p.add_argument("file")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser(
        "diff", parents=[module], help="compare two snapshots of the same program"
    )
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
