#!/usr/bin/env python3
"""Read, inspect and rebuild wasm3 snapshots (.dmp / .wasm).

A snapshot holds the execution state of a suspended runtime: linear memory,
globals, tables, which segments were dropped, every continuation and exception
the suspended program can still reach - the root continuation that is the
paused call itself among them - and whatever state the embedder saved with it.
`m3_SaveSnapshot` writes it and `m3_LoadSnapshot` reads it back.

Snapshots can exist as standalone binary files (magic `\0dmp` plus a 4-byte version, currently 1)
or embedded directly as custom sections (`"snapshot"` or `"snapshot.<name>"`)
inside standard `.wasm` modules.

As a command:

    snapshot-tool.py info    run.dmp [--wasm run.wasm]
    snapshot-tool.py info    app.wasm[:<name>]
    snapshot-tool.py unpack  run.dmp -o run.d [--wasm run.wasm]
    snapshot-tool.py unpack  app.wasm[:<name>] -o run.d
    snapshot-tool.py pack    run.d -o run.dmp
    snapshot-tool.py verify  run.dmp [--wasm run.wasm]
    snapshot-tool.py verify  app.wasm[:<name>]
    snapshot-tool.py embed   run.dmp --wasm app.wasm -o app_with_snap.wasm[:<name>]
    snapshot-tool.py extract app_with_snap.wasm[:<name>] [-o run.dmp]

As a module:

    import importlib.util, pathlib
    spec = importlib.util.spec_from_file_location("snapshot", "extra/snapshot-tool.py")
    snaptool = importlib.util.module_from_spec(spec); spec.loader.exec_module(snaptool)

    snap = snaptool.load("run.dmp")
    print(snap.globals, snap.root.activations)
    snap.memories[0].data          # bytes, expanded from the chunk encoding
    open("out.dmp", "wb").write(snaptool.pack(snap))
"""

import argparse
import json
import os
import struct
import sys

MAGIC = b"\x00dmp"
MAGIC_WASM = b"\x00asm"
SUPPORTED_VERSIONS = (1,)

SECTION_META = 0
SECTION_MEMORY = 1
SECTION_TABLE = 2
SECTION_GLOBAL = 3
SECTION_SEGMENT = 4
SECTION_EXCEPTION = 5
SECTION_CONTINUATION = 6
SECTION_HOST_STATE = 7

# Wasm ValTypes
VALTYPE_I32 = 0x7F
VALTYPE_I64 = 0x7E
VALTYPE_F32 = 0x7D
VALTYPE_F64 = 0x7C
VALTYPE_V128 = 0x7B
VALTYPE_FUNCREF = 0x70
VALTYPE_EXTERNREF = 0x6F
VALTYPE_EXNREF = 0x69
VALTYPE_CONTREF = 0x68

TYPE_NAMES = {
    0x00: "none",
    VALTYPE_I32: "i32",
    VALTYPE_I64: "i64",
    VALTYPE_F32: "f32",
    VALTYPE_F64: "f64",
    VALTYPE_V128: "v128",
    VALTYPE_FUNCREF: "funcref",
    VALTYPE_EXTERNREF: "externref",
    VALTYPE_EXNREF: "exnref",
    VALTYPE_CONTREF: "contref",
}
VALUE_TYPES = (
    VALTYPE_I32,
    VALTYPE_I64,
    VALTYPE_F32,
    VALTYPE_F64,
    VALTYPE_V128,
    VALTYPE_FUNCREF,
    VALTYPE_EXTERNREF,
    VALTYPE_EXNREF,
    VALTYPE_CONTREF,
)
REF_TYPES = (VALTYPE_FUNCREF, VALTYPE_EXTERNREF, VALTYPE_EXNREF, VALTYPE_CONTREF)

# Ref kinds
REF_KIND_NULL = 0x00
REF_KIND_FUNC = 0x01
REF_KIND_EXTERN = 0x02
REF_KIND_EXN = 0x03
REF_KIND_CONT = 0x04

FLAG_POSTMORTEM = 0x1

NONE_INDEX = 0xFFFFFFFF
NULL_REF = 0xFFFFFFFFFFFFFFFF

CHUNK_END = 0x00
CHUNK_RAW = 0x01
CHUNK_FILL_FF = 0x02

CONT_ALLOCATED = 0
CONT_SUSPENDED = 1
CONT_FINISHED = 2
CONT_STATES = {
    CONT_ALLOCATED: "allocated",
    CONT_SUSPENDED: "suspended",
    CONT_FINISHED: "finished",
}

# M3SafePointKind: where a function's frame can be left standing
SITE_OP = 0
SITE_SUSPEND = 1
SITE_CALL = 2
SITE_RESUME = 3
SITE_ENTRY = 4
SITE_KINDS = {
    SITE_OP: "back edge",
    SITE_SUSPEND: "suspend",
    SITE_CALL: "call",
    SITE_RESUME: "resume",
    SITE_ENTRY: "entry",
}
# what the innermost function of a continuation can have stopped at
INNERMOST_SITES = (SITE_OP, SITE_SUSPEND, SITE_RESUME, SITE_ENTRY)


class FormatError(Exception):
    pass


# --------------------------------------------------------------------------- model


class Memory:
    __slots__ = ("_data", "chunks", "has_data", "max_pages", "num_pages", "page_size")

    def __init__(self, num_pages, max_pages, page_size, has_data, chunks):
        self.num_pages = num_pages
        self.max_pages = max_pages  # None when the module is not known
        self.page_size = page_size  # the module's, so None when it is not known
        self.has_data = has_data
        self.chunks = chunks  # list of (kind, offset, payload-or-length)
        self._data = None

    @property
    def size(self):
        """Bytes, or None: the file counts pages, and the module says how big one is."""
        if self.page_size is None:
            return None
        return self.num_pages * self.page_size

    @property
    def extent(self):
        """How far the chunks reach, which is all there is to go on without the module."""
        return max(
            (off + (len(p) if k == CHUNK_RAW else p) for k, off, p in self.chunks),
            default=0,
        )

    @property
    def data(self):
        """The linear memory, expanded - as far as the chunks reach when the
        page size is not known, since everything past them is zero anyway."""
        if self._data is None:
            size = self.size if self.size is not None else self.extent
            buf = bytearray(size if self.has_data else 0)
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
        size = (self.size if self.has_data else 0) if self.size is not None else None
        return {
            "pages": self.num_pages,
            "max_pages": self.max_pages,
            "page_size": self.page_size,
            "has_data": self.has_data,
            "bytes": size,
            "stored_bytes": raw,
            "filled_ff_bytes": ff,
            "implicit_zero_bytes": size - raw - ff if size is not None else None,
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


class Activation(Record):
    """One function's frame in a suspended continuation: where it stands and
    what it holds there - its locals, then its operand stack. A back edge also
    says which loop it goes to, by that loop's offset."""

    __slots__ = (
        "cont_id",
        "func_index",
        "site",
        "target_loop",
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
    """Everything a snapshot file holds."""

    def __init__(self):
        self.version = 1
        self.flags = 0
        self.timestamp_ms = 0
        self.module_hash = 0
        self.exceptions = []
        self.memories = []
        self.globals = []  # list of (type, u64 word)
        self.tables = []  # list of (element type, list of u64 words)
        self.data_dropped = []
        self.elem_dropped = []
        self.continuations = []
        self.host_state = b""
        self.section_order = []  # section IDs in the order the file had them
        self.extra_sections = {}  # ID -> body, for sections this tool does not know
        self.module = None  # ModuleInfo, if a module was given
        self.raw = b""  # the container bytes it was read from, if any

    # ---- convenience

    @property
    def is_postmortem(self):
        return bool(self.flags & FLAG_POSTMORTEM)

    @property
    def root(self):
        for c in self.continuations:
            if c and c.is_root:
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
            if type_id == VALTYPE_FUNCREF:
                return f"func {self.describe_func(word)}"
            if type_id == VALTYPE_EXNREF:
                return f"exception #{word}"
            if type_id == VALTYPE_CONTREF:
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
                "magic": "dmp",
                "version": self.version,
                "flags": self.flags,
                "postmortem": self.is_postmortem,
                "timestamp_ms": self.timestamp_ms,
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
            "continuations": [_cont_to_dict(self, c) for c in self.continuations if c],
            "host_state_bytes": len(self.host_state),
        }


def _activation_to_dict(snap, a):
    d = {
        "func_index": a.func_index,
        "func_name": snap.func_name(a.func_index),
        "site": a.site_name,
        "site_id": a.site,
        "wasm_offset": a.wasm_offset,
        "values": [
            {"type": TYPE_NAMES.get(t, f"type{t}"), "type_id": t, "word": w}
            for t, w in a.values
        ],
    }
    num_locals = snap.num_locals(a.func_index)
    if num_locals is not None:
        d["num_locals"] = num_locals
    if a.site == SITE_OP:
        d["target_loop"] = a.target_loop
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
    if type_id == VALTYPE_I32:  # i32
        v = value & 0xFFFFFFFF
        return v - (1 << 32) if v >> 31 else v
    if type_id == VALTYPE_I64:  # i64
        return value - (1 << 64) if value >> 63 else value
    if type_id == VALTYPE_F32:  # f32
        return struct.unpack("<f", struct.pack("<I", value & 0xFFFFFFFF))[0]
    if type_id == VALTYPE_F64:  # f64
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

    def u8(self):
        return self.take(1)[0]

    def _leb(self, bits, signed=False):
        value = 0
        for shift in range(0, bits, 7):
            byte = self.u8()
            value |= (byte & 0x7F) << shift
            if not byte & 0x80:
                width = shift + 7
                if signed and byte & 0x40:
                    value -= 1 << width
                low = -(1 << (bits - 1)) if signed else 0
                high = (1 << (bits - 1)) - 1 if signed else (1 << bits) - 1
                if not low <= value <= high:
                    raise FormatError(f"LEB128 overflow ({bits} bits)")
                return value
        raise FormatError(f"LEB128 overflow ({bits} bits)")

    def leb_u32(self):
        return self._leb(32)

    def leb_u64(self):
        return self._leb(64)

    def leb_i32(self):
        return self._leb(32, signed=True)

    def leb_i64(self):
        return self._leb(64, signed=True)

    def boolean(self):
        value = self.u8()
        if value > 1:
            raise FormatError(f"invalid boolean {value}")
        return value

    def utf8(self):
        length = self.leb_u32()
        try:
            return self.take(length).decode("utf-8")
        except UnicodeDecodeError as e:
            raise FormatError("invalid UTF-8 name") from e

    def read_ref_value(self, expected_kind=None):
        kind = self.u8()
        if kind == REF_KIND_NULL:
            return (REF_KIND_NULL, NULL_REF)
        if kind not in (REF_KIND_FUNC, REF_KIND_EXTERN, REF_KIND_EXN, REF_KIND_CONT):
            raise FormatError(f"unknown reference kind {kind}")
        if expected_kind is not None and kind != expected_kind:
            raise FormatError("reference kind does not match its value type")
        word = self.leb_u64()
        return (kind, word)

    def read_value(self):
        valtype = self.u8()
        if valtype in REF_TYPES:
            kind, word = self.read_ref_value(REF_TYPES.index(valtype) + 1)
            return (valtype, word)
        elif valtype == VALTYPE_I32:
            v = self.leb_i32()
            return (valtype, v & 0xFFFFFFFF)
        elif valtype == VALTYPE_I64:
            v = self.leb_i64()
            return (valtype, v & 0xFFFFFFFFFFFFFFFF)
        elif valtype == VALTYPE_F32:
            bits = struct.unpack("<I", self.take(4))[0]
            return (valtype, bits)
        elif valtype == VALTYPE_F64:
            bits = struct.unpack("<Q", self.take(8))[0]
            return (valtype, bits)
        elif valtype == VALTYPE_V128:
            raw = self.take(16)
            return (valtype, raw)
        else:
            raise FormatError(
                f"unknown valtype 0x{valtype:02x} at offset {self.pos - 1}"
            )

    @property
    def remaining(self):
        return len(self.data) - self.pos


def _sane(count, what, cursor):
    """A count no larger than the bytes left to hold it: every element takes
    at least one, so a larger one is damage rather than a big snapshot."""
    if count > cursor.remaining:
        raise FormatError(f"{what} count {count} is more than the data holds")
    return count


def _read_activation(c):
    a = Activation(func_index=c.leb_u32())
    a.site = c.u8()
    a.wasm_offset = c.leb_u32()
    if a.site == SITE_OP:
        a.target_loop = c.leb_u32()
    a.values = [c.read_value() for _ in range(_sane(c.leb_u32(), "values", c))]
    if a.site == SITE_RESUME:
        ref_kind, ref_id = c.read_ref_value(REF_KIND_CONT)
        if ref_kind == REF_KIND_NULL or ref_id == 0:
            raise FormatError("a resume must target a non-root continuation")
        a.cont_id = ref_id
    elif a.site not in SITE_KINDS:
        raise FormatError(f"unknown safepoint kind {a.site} at offset {c.pos}")
    return a


def _read_continuation(c, snap, cont_id):
    c_id = c.leb_u32()
    if c_id != cont_id:
        raise FormatError("continuation IDs must be in order")
    state = c.u8()
    if state not in CONT_STATES:
        raise FormatError(f"unknown continuation state {state}")
    is_root = bool(c.boolean())
    type_index = c.leb_u32()
    entry_func_index = c.leb_u32()
    resume_throw_kind, resume_throw_id = c.read_ref_value(REF_KIND_EXN)
    if entry_func_index == NONE_INDEX:
        raise FormatError(f"continuation {cont_id} names no function")
    if state == CONT_FINISHED and resume_throw_kind != REF_KIND_NULL:
        raise FormatError(f"finished continuation {cont_id} has an exception to raise")

    k = Continuation(
        id=cont_id,
        state=state,
        is_root=is_root,
        type_index=type_index,
        entry_func_index=entry_func_index,
        bound_args_count=0,
        resume_throw=resume_throw_id,
    )
    # a postmortem has nothing to resume, so it stops here and says no more
    if snap.is_postmortem:
        if is_root:
            raise FormatError("a postmortem has no root continuation")
        return k

    if is_root != (cont_id == 0):
        raise FormatError("only continuation zero is the root")
    if is_root and (type_index != NONE_INDEX or state != CONT_SUSPENDED):
        raise FormatError("the root must be suspended and have no continuation type")
    if not is_root and type_index == NONE_INDEX:
        raise FormatError("a non-root continuation must name its type")

    # Allocated bindings are stored here; suspended bindings occupy the
    # innermost suspension's result slots and only need their count here.
    k.bound_args_count = c.leb_u32()
    if is_root and k.bound_args_count:
        raise FormatError("the root cannot have bound arguments")

    if k.state == CONT_ALLOCATED:
        k.args = [
            c.read_value()
            for _ in range(_sane(k.bound_args_count, "bound arguments", c))
        ]
    elif k.state == CONT_SUSPENDED:
        count = _sane(c.leb_u32(), "activations", c)
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
    elif k.bound_args_count:
        raise FormatError(f"finished continuation {cont_id} has bound arguments")
    return k


def _wasm_sections(data):
    """Walk bounded Wasm sections, retaining their original framing."""
    c = _Cursor(data)
    if c.take(8) != MAGIC_WASM + struct.pack("<I", 1):
        raise FormatError("not a version 1 wasm module")
    while c.remaining:
        start = c.pos
        section_id = c.u8()
        body = c.take(c.leb_u32())
        name = None
        if section_id == 0:
            custom = _Cursor(body)
            name = custom.utf8()
            body = custom.take(custom.remaining)
        yield section_id, start, c.pos, name, body


_XXH_MASK = (1 << 64) - 1
_XXH_P1 = 0x9E3779B185EBCA87
_XXH_P2 = 0xC2B2AE3D27D4EB4F
_XXH_P3 = 0x165667B19E3779F9
_XXH_P4 = 0x85EBCA77C2B2AE63
_XXH_P5 = 0x27D4EB2F165667C5


def _xxh_rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & _XXH_MASK


def _xxh_round(acc, val):
    return (_xxh_rotl((acc + val * _XXH_P2) & _XXH_MASK, 31) * _XXH_P1) & _XXH_MASK


def xxh64(data, seed=0):
    """XXH64 of a byte string. Matches the reference implementation."""
    n, i = len(data), 0
    word = lambda o: int.from_bytes(data[o : o + 8], "little")
    if n >= 32:
        acc = [(seed + _XXH_P1 + _XXH_P2) & _XXH_MASK, (seed + _XXH_P2) & _XXH_MASK]
        acc += [seed, (seed - _XXH_P1) & _XXH_MASK]
        while n - i >= 32:
            for lane in range(4):
                acc[lane] = _xxh_round(acc[lane], word(i))
                i += 8
        h = (
            _xxh_rotl(acc[0], 1)
            + _xxh_rotl(acc[1], 7)
            + _xxh_rotl(acc[2], 12)
            + _xxh_rotl(acc[3], 18)
        ) & _XXH_MASK
        for lane in range(4):
            h = ((h ^ _xxh_round(0, acc[lane])) * _XXH_P1 + _XXH_P4) & _XXH_MASK
    else:
        h = (seed + _XXH_P5) & _XXH_MASK
    h = (h + n) & _XXH_MASK
    while n - i >= 8:
        h = (_xxh_rotl(h ^ _xxh_round(0, word(i)), 27) * _XXH_P1 + _XXH_P4) & _XXH_MASK
        i += 8
    if n - i >= 4:
        chunk = int.from_bytes(data[i : i + 4], "little") * _XXH_P1 & _XXH_MASK
        h = (_xxh_rotl(h ^ chunk, 23) * _XXH_P2 + _XXH_P3) & _XXH_MASK
        i += 4
    while i < n:
        h = (_xxh_rotl(h ^ (data[i] * _XXH_P5 & _XXH_MASK), 11) * _XXH_P1) & _XXH_MASK
        i += 1
    h ^= h >> 33
    h = (h * _XXH_P2) & _XXH_MASK
    h ^= h >> 29
    h = (h * _XXH_P3) & _XXH_MASK
    return h ^ (h >> 32)


def module_hash(wasm_data):
    """The meta section's wasm_hash: XXH64 over the non-custom sections, framing
    included, in module order. See docs/proposals/Snapshot.md."""
    body = b"".join(
        wasm_data[start:end]
        for kind, start, end, _, _ in _wasm_sections(wasm_data)
        if kind
    )
    return xxh64(body)


def _snapshot_name(name):
    if name == "snapshot":
        return ""
    if name and name.startswith("snapshot.") and len(name) > 9:
        return name[9:]
    return None


class _Duplicate:
    """Stands in for a snapshot name that more than one custom section carries.
    The module stays valid; only selecting or replacing that name is refused."""

    def __init__(self, name):
        self.name = name


def extract_snapshots_from_wasm(data):
    """Returns {name: section_stream} without a standalone container header.

    A name carried by more than one section maps to a _Duplicate instead of a
    stream, so the caller that selects it can refuse it."""
    snapshots = {}
    for _, _, _, custom_name, body in _wasm_sections(data):
        name = _snapshot_name(custom_name)
        if name is not None:
            if name in snapshots:
                snapshots[name] = _Duplicate(name)
            else:
                snapshots[name] = body
    return snapshots


def embed_snapshot_in_wasm(wasm_data, snapshot_payload, name=None):
    """Embed a standalone snapshot, replacing the checkpoint of the same name."""
    # A duplicate of the name being replaced is refused rather than choosing one.
    snaps = extract_snapshots_from_wasm(wasm_data)
    key = name or ""
    if isinstance(snaps.get(key), _Duplicate):
        raise FormatError(f"duplicate embedded snapshot name: {key!r}")
    snap = _parse(snapshot_payload)
    if snap.is_postmortem:
        raise FormatError("a postmortem cannot be embedded in a module")
    if snapshot_payload[:4] != MAGIC:
        raise FormatError("embedding requires a standalone snapshot")
    # running the module resumes what it carries, so that has to be its own
    if snap.module_hash != module_hash(wasm_data):
        raise FormatError("the snapshot belongs to another module")
    sec_name = f"snapshot.{name}" if name else "snapshot"
    out = bytearray(wasm_data[:8])
    for _, start, end, custom_name, _ in _wasm_sections(wasm_data):
        if custom_name != sec_name:
            out.extend(wasm_data[start:end])

    w = _Writer()
    w.utf8(sec_name)
    w.raw(snapshot_payload[8:])
    sw = _Writer()
    sw.u8(0)
    sw.leb_u32(len(w.out))
    sw.raw(w.out)
    return bytes(out + sw.out)


def _select_snapshot(snaps, name, where):
    """The embedded snapshot of that name; no name is the unnamed default."""
    if not snaps:
        raise FormatError(f"{where} contains no snapshot custom section")
    key = name or ""
    if key not in snaps:
        if name:
            raise FormatError(f"snapshot '{name}' not found in {where}")
        raise FormatError(
            f"{where} holds no default snapshot, only named ones: "
            + ", ".join(sorted(snaps))
        )
    val = snaps[key]
    if isinstance(val, _Duplicate):
        raise FormatError(f"duplicate embedded snapshot name: {key!r}")
    return val


def _parse(data, snapshot_name=None):
    if len(data) < 5:
        raise FormatError("too short to be a snapshot")

    # If it's a wasm binary, extract the embedded snapshot
    if data[:4] == MAGIC_WASM:
        payload = _select_snapshot(
            extract_snapshots_from_wasm(data), snapshot_name, "wasm module"
        )
        data = MAGIC + struct.pack("<I", 1) + payload

    if data[:4] != MAGIC:
        raise FormatError(f"not a snapshot (magic is {data[:4]!r})")

    c = _Cursor(data)
    c.take(4)  # skip magic
    version = struct.unpack("<I", c.take(4))[0]
    if version not in SUPPORTED_VERSIONS:
        raise FormatError(f"unsupported version {version}")

    s = Snapshot()
    s.version = version
    s.raw = data

    seen = set()
    while c.remaining > 0:
        sec_id = c.u8()
        if not seen and sec_id != SECTION_META:
            raise FormatError("Meta must be the first section")
        if sec_id in seen:
            raise FormatError(f"duplicate section {sec_id}")
        seen.add(sec_id)
        s.section_order.append(sec_id)
        sec_size = c.leb_u32()
        if not sec_size:
            raise FormatError("empty section")
        sec_data = c.take(sec_size)
        sc = _Cursor(sec_data)

        if sec_id == SECTION_META:
            s.flags = sc.leb_u32()
            if s.flags & ~FLAG_POSTMORTEM:
                raise FormatError(f"unknown snapshot flags 0x{s.flags:x}")
            s.timestamp_ms = sc.leb_u64()
            s.module_hash = sc.leb_u64()
            # the records are in other sections, so it is the file that bounds them
            num_conts = _sane(sc.leb_u32(), "continuations", c)
            num_exns = _sane(sc.leb_u32(), "exceptions", sc)
            for _ in range(num_exns):
                tag_idx = sc.leb_u32()
                s.exceptions.append(Exception_(tag_index=tag_idx, args=[]))
            s.continuations = [None] * num_conts

        elif sec_id == SECTION_MEMORY:
            num_mems = _present(sc.leb_u32(), "memories")
            for i in range(num_mems):
                mem_idx = sc.leb_u32()
                if mem_idx != i:
                    raise FormatError("memories are listed out of index order")
                num_pages = sc.leb_u32()
                chunks = []
                while True:
                    kind = sc.u8()
                    if kind == CHUNK_END:
                        break
                    if kind not in (CHUNK_RAW, CHUNK_FILL_FF):
                        raise FormatError(f"unknown memory chunk kind {kind}")
                    off = sc.leb_u32()
                    ln = sc.leb_u32()
                    chunks.append((kind, off, sc.take(ln) if kind == CHUNK_RAW else ln))
                has_data = len(chunks) > 0
                s.memories.append(Memory(num_pages, None, None, has_data, chunks))

        elif sec_id == SECTION_TABLE:
            num_tables = _present(sc.leb_u32(), "tables")
            for i in range(num_tables):
                table_idx = sc.leb_u32()
                if table_idx != i:
                    raise FormatError("tables are listed out of index order")
                valtype = sc.u8()
                if valtype not in REF_TYPES:
                    raise FormatError("table has a non-reference type")
                size = _sane(sc.leb_u32(), "table elements", sc)
                elems = []
                for _ in range(size):
                    ref_kind, ref_id = sc.read_ref_value(REF_TYPES.index(valtype) + 1)
                    elems.append(ref_id)
                s.tables.append((valtype, elems))

        elif sec_id == SECTION_GLOBAL:
            num_globals = _present(sc.leb_u32(), "globals")
            for i in range(num_globals):
                glob_idx = sc.leb_u32()
                if glob_idx != i:
                    raise FormatError("globals are listed out of index order")
                valtype, val = sc.read_value()
                s.globals.append((valtype, val))

        elif sec_id == SECTION_SEGMENT:
            num_data = sc.leb_u32()
            s.data_dropped = [sc.boolean() for _ in range(num_data)]
            num_elem = sc.leb_u32()
            s.elem_dropped = [sc.boolean() for _ in range(num_elem)]
            _present(num_data + num_elem, "segments")

        elif sec_id == SECTION_EXCEPTION:
            num_exns = sc.leb_u32()
            if not num_exns or num_exns != len(s.exceptions):
                raise FormatError("exception count does not match Meta")
            for i in range(num_exns):
                if sc.leb_u32() != i:
                    raise FormatError("exception IDs must be in order")
                num_args = sc.leb_u32()
                s.exceptions[i].args = [sc.read_value() for _ in range(num_args)]

        elif sec_id == SECTION_CONTINUATION:
            num_conts = _present(sc.leb_u32(), "continuations")
            if num_conts != len(s.continuations):
                raise FormatError("continuation count does not match Meta")
            for i in range(num_conts):
                s.continuations[i] = _read_continuation(sc, s, i)

        elif sec_id == SECTION_HOST_STATE:
            s.host_state = sc.take(sc.remaining)  # the whole section is the embedder's
        else:
            s.extra_sections[sec_id] = sc.take(sc.remaining)  # opaque, but kept

        if sc.remaining:
            raise FormatError(f"section {sec_id} does not consume its declared length")

    if SECTION_META not in seen:
        raise FormatError("missing Meta section")
    if not s.is_postmortem and SECTION_CONTINUATION not in seen:
        raise FormatError("missing Continuation section")
    if s.continuations and SECTION_CONTINUATION not in seen:
        raise FormatError("Meta counts continuations the file does not hold")
    if s.exceptions and SECTION_EXCEPTION not in seen:
        raise FormatError("missing Exception section")
    return s


def _present(count, what):
    """A section with nothing to say is left out, never written empty."""
    if not count:
        raise FormatError(f"a section that lists no {what}")
    return count


def _parse_wasm_path(path):
    """Splits "<path>.wasm:<name>" at the last ".wasm:", as the CLI does, so a
    directory with one in its name does not end the path early."""
    if path and isinstance(path, str) and ".wasm:" in path:
        file_path, name = path.rsplit(".wasm:", 1)
        return file_path + ".wasm", name
    return path, None


def load(source, *, module=None, name=None):
    """Read a snapshot from a path, a file object or bytes.

    module is a path to the .wasm the snapshot belongs to, used to put names to
    function indices, to tell a function's locals from its operand stack, and
    to turn offsets in a body into offsets in the module.
    """
    wasm_module_path = module
    if isinstance(source, str):
        source_file, snap_name = _parse_wasm_path(source)
        if snap_name is not None:
            source = source_file
            if name is None:
                name = snap_name
            if wasm_module_path is None:
                wasm_module_path = source_file
        elif source.endswith(".wasm") and wasm_module_path is None:
            wasm_module_path = source

    if isinstance(source, (bytes, bytearray)):
        data = bytes(source)
    elif hasattr(source, "read"):
        data = source.read()
    else:
        with open(source, "rb") as f:
            data = f.read()

    # an embedded snapshot belongs to the module that carries it, and to no other
    if data[:4] == MAGIC_WASM and wasm_module_path is not None:
        same = isinstance(source, str) and os.path.samefile(source, wasm_module_path)
        if not same:
            raise FormatError(
                "an embedded snapshot belongs to the module that carries it; "
                "extract it to read it against another"
            )

    snap = _parse(data, snapshot_name=name)
    if wasm_module_path:
        snap.module = ModuleInfo.read(wasm_module_path)
        for m, (page_size, max_pages) in zip(snap.memories, snap.module.memories):
            m.page_size = page_size
            m.max_pages = max_pages
    return snap


# --------------------------------------------------------------------------- writing


class _Writer:
    def __init__(self):
        self.out = bytearray()

    def u8(self, v):
        self.out.append(int(v) & 0xFF)

    def leb_u32(self, v):
        v = int(v) & 0xFFFFFFFF
        while True:
            byte = v & 0x7F
            v >>= 7
            if v == 0:
                self.out.append(byte)
                break
            self.out.append(byte | 0x80)

    def leb_u64(self, v):
        v = int(v) & 0xFFFFFFFFFFFFFFFF
        while True:
            byte = v & 0x7F
            v >>= 7
            if v == 0:
                self.out.append(byte)
                break
            self.out.append(byte | 0x80)

    def leb_i64(self, v):
        v = int(v)
        more = True
        while more:
            byte = v & 0x7F
            v >>= 7
            if (v == 0 and not (byte & 0x40)) or (v == -1 and (byte & 0x40)):
                more = False
            else:
                byte |= 0x80
            self.out.append(byte)

    def utf8(self, s):
        raw = s.encode("utf-8") if s else b""
        self.leb_u32(len(raw))
        self.out.extend(raw)

    def raw(self, b):
        self.out.extend(b)

    def write_ref_value(self, kind, word):
        if word == NULL_REF or kind == REF_KIND_NULL:
            self.u8(REF_KIND_NULL)
        else:
            self.u8(kind)
            self.leb_u64(word)

    def write_value(self, valtype, val):
        self.u8(valtype)
        if valtype in REF_TYPES:
            kind = (
                REF_KIND_FUNC
                if valtype == VALTYPE_FUNCREF
                else (
                    REF_KIND_EXTERN
                    if valtype == VALTYPE_EXTERNREF
                    else (REF_KIND_EXN if valtype == VALTYPE_EXNREF else REF_KIND_CONT)
                )
            )
            self.write_ref_value(kind, val)
        elif valtype == VALTYPE_I32:
            s = val - (1 << 32) if (val & 0xFFFFFFFF) >> 31 else val & 0xFFFFFFFF
            self.leb_i64(s)
        elif valtype == VALTYPE_I64:
            s = (
                val - (1 << 64)
                if (val & 0xFFFFFFFFFFFFFFFF) >> 63
                else val & 0xFFFFFFFFFFFFFFFF
            )
            self.leb_i64(s)
        elif valtype == VALTYPE_F32:
            self.raw(struct.pack("<I", val & 0xFFFFFFFF))
        elif valtype == VALTYPE_F64:
            self.raw(struct.pack("<Q", val & 0xFFFFFFFFFFFFFFFF))
        elif valtype == VALTYPE_V128:
            self.raw(val if isinstance(val, (bytes, bytearray)) else bytes(16))
        else:
            raise FormatError(f"unknown valtype 0x{valtype:02x}")


def _write_activation(w, a):
    w.leb_u32(a.func_index)
    w.u8(a.site)
    w.leb_u32(a.wasm_offset)
    if a.site == SITE_OP:
        w.leb_u32(a.target_loop)
    w.leb_u32(len(a.values))
    for type_id, word in a.values:
        w.write_value(type_id, word)
    if a.site == SITE_RESUME:
        w.write_ref_value(REF_KIND_CONT, a.cont_id)


def _write_continuation(w, snap, k):
    w.leb_u32(k.id)
    w.u8(k.state)
    w.u8(k.is_root)
    w.leb_u32(k.type_index)
    w.leb_u32(k.entry_func_index)
    w.write_ref_value(
        REF_KIND_EXN, k.resume_throw if k.resume_throw is not None else NULL_REF
    )

    if snap.is_postmortem:
        return

    if k.state == CONT_ALLOCATED:
        w.leb_u32(len(k.args) if k.args else 0)
        if k.args:
            for item in k.args:
                if isinstance(item, (tuple, list)):
                    w.write_value(item[0], item[1])
                else:
                    w.write_value(VALTYPE_I64, item)
    else:
        w.leb_u32(k.bound_args_count)

        if k.state == CONT_SUSPENDED:
            w.leb_u32(len(k.activations) if k.activations else 0)
            if k.activations:
                for a in k.activations:
                    _write_activation(w, a)


def pack(snap):
    """Serialize a Snapshot back to bytes, matching wasm3's format.

    A section with nothing to say is left out. Sections go in the order the
    file they were read from had them, sections this tool does not know
    included, and any new one after those in ID order - so what was read
    comes back byte for byte."""
    bodies = {}

    # Section 0: Meta
    sw = _Writer()
    sw.leb_u32(snap.flags)
    sw.leb_u64(snap.timestamp_ms)
    sw.leb_u64(snap.module_hash)
    sw.leb_u32(len(snap.continuations))
    sw.leb_u32(len(snap.exceptions))
    for e in snap.exceptions:
        sw.leb_u32(e.tag_index)
    bodies[SECTION_META] = sw.out

    # Section 1: Memory
    sw = _Writer()
    sw.leb_u32(len(snap.memories))
    for i, m in enumerate(snap.memories):
        sw.leb_u32(i)
        sw.leb_u32(m.num_pages)
        if m.has_data:
            for kind, off, payload in m.chunks:
                sw.u8(kind)
                sw.leb_u32(off)
                if kind == CHUNK_RAW:
                    sw.leb_u32(len(payload))
                    sw.raw(payload)
                else:
                    sw.leb_u32(payload)
        sw.u8(CHUNK_END)
    if snap.memories:
        bodies[SECTION_MEMORY] = sw.out

    # Section 2: Table
    sw = _Writer()
    sw.leb_u32(len(snap.tables))
    for i, (valtype, elems) in enumerate(snap.tables):
        sw.leb_u32(i)
        sw.u8(valtype)
        sw.leb_u32(len(elems))
        kind = (
            REF_KIND_FUNC
            if valtype == VALTYPE_FUNCREF
            else (
                REF_KIND_EXTERN
                if valtype == VALTYPE_EXTERNREF
                else (REF_KIND_EXN if valtype == VALTYPE_EXNREF else REF_KIND_CONT)
            )
        )
        for e in elems:
            sw.write_ref_value(kind, e)
    if snap.tables:
        bodies[SECTION_TABLE] = sw.out

    # Section 3: Global
    sw = _Writer()
    sw.leb_u32(len(snap.globals))
    for i, (valtype, val) in enumerate(snap.globals):
        sw.leb_u32(i)
        sw.write_value(valtype, val)
    if snap.globals:
        bodies[SECTION_GLOBAL] = sw.out

    # Section 4: Segment
    sw = _Writer()
    sw.leb_u32(len(snap.data_dropped))
    for d in snap.data_dropped:
        sw.u8(d)
    sw.leb_u32(len(snap.elem_dropped))
    for d in snap.elem_dropped:
        sw.u8(d)
    if snap.data_dropped or snap.elem_dropped:
        bodies[SECTION_SEGMENT] = sw.out

    # Section 5: Exception
    if snap.exceptions:
        sw = _Writer()
        sw.leb_u32(len(snap.exceptions))
        for i, e in enumerate(snap.exceptions):
            sw.leb_u32(i)
            sw.leb_u32(len(e.args))
            for a in e.args:
                if isinstance(a, (tuple, list)):
                    sw.write_value(a[0], a[1])
                else:
                    sw.write_value(VALTYPE_I64, a)
        bodies[SECTION_EXCEPTION] = sw.out

    # Section 6: Continuation
    if snap.continuations:
        sw = _Writer()
        sw.leb_u32(len(snap.continuations))
        for k in snap.continuations:
            _write_continuation(sw, snap, k)
        bodies[SECTION_CONTINUATION] = sw.out

    # Section 7: Host State, which is the embedder's bytes and nothing else
    if snap.host_state:
        bodies[SECTION_HOST_STATE] = bytes(snap.host_state)

    for sec_id, body in snap.extra_sections.items():
        if body:
            bodies[sec_id] = body

    order = [i for i in snap.section_order if i in bodies]
    order += sorted(i for i in bodies if i not in order)
    if order[0] != SECTION_META:
        order.remove(SECTION_META)
        order.insert(0, SECTION_META)

    w = _Writer()
    w.raw(MAGIC)
    w.raw(struct.pack("<I", snap.version))
    for sec_id in order:
        w.u8(sec_id)
        w.leb_u32(len(bodies[sec_id]))
        w.raw(bodies[sec_id])
    return bytes(w.out)


def encode_memory(data, run_threshold=128):
    """Chunk-encode linear memory the way wasm3 does."""
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
    """What a snapshot needs from its module to be read in Wasm's terms."""

    def __init__(self):
        self.names = {}
        self.num_imported = 0
        self.func_types = []  # type index per function, imports first
        self.type_params = {}  # type index -> param count, for function types
        self.memories = []  # (page size, maximum pages or None), imports first
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
            if data[p] in (0x63, 0x64):
                return leb(p + 1)[1]
            return p + 1

        def limits(p):
            """Past a limits encoding, with its maximum and page size."""
            flags = data[p]
            _, p = leb(p + 1)
            maximum = page_size = None
            if flags & 0x01:
                maximum, p = leb(p)
            if flags & 0x08:  # custom page sizes: log2 of the page
                log2, p = leb(p)
                page_size = 1 << log2
            return p, maximum, page_size

        def memory(p):
            p, maximum, page_size = limits(p)
            self.memories.append((page_size or 65536, maximum))
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
                        if data[p] in (0x50, 0x4F):
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
                        p = limits(value_type(p))[0]
                    elif kind == 0x02:  # memory
                        p = memory(p)
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

            elif section_id == 5:  # memories
                count, p = leb(pos)
                for _ in range(count):
                    p = memory(p)

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
        if type_id == VALTYPE_EXNREF and word >= num_exns:
            problems.append(f"{where}: exception #{word} is not in the snapshot")
        elif type_id == VALTYPE_CONTREF and word >= num_conts:
            problems.append(f"{where}: continuation #{word} is not in the snapshot")
        elif (
            type_id == VALTYPE_FUNCREF
            and snap.module
            and word >= len(snap.module.func_types)
        ):
            problems.append(f"{where}: function {word} is not in the module")

    for i, m in enumerate(snap.memories):
        if m.max_pages is not None and m.num_pages > m.max_pages:
            problems.append(
                f"memory {i}: {m.num_pages} pages exceeds its maximum of {m.max_pages}"
            )
        # without the module there is no page size, and so no bound to check
        if m.size is None:
            continue
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

    roots = [k for k in snap.continuations if k and k.is_root]
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
        if roots:
            root = roots[0]
            if root.type_index != NONE_INDEX:
                problems.append("the root continuation must have no continuation type")
            if root.bound_args_count:
                problems.append("the root continuation cannot have bound arguments")
            if root.resume_throw is not None and root.resume_throw != NULL_REF:
                problems.append(
                    "the root continuation cannot carry a resume_throw exception"
                )

    # a continuation runs under one resume at a time
    targets = [
        a.cont_id
        for k in snap.continuations
        if k and k.activations
        for a in k.activations
        if a.site == SITE_RESUME
    ]
    for cont_id in sorted({t for t in targets if targets.count(t) > 1}):
        problems.append(f"continuation #{cont_id} is resumed by more than one frame")

    # resume links must form acyclic chains
    resume_target = {}
    for k in snap.continuations:
        if k and k.activations and k.activations[-1].site == SITE_RESUME:
            resume_target[k.id] = k.activations[-1].cont_id

    for start_id in sorted(resume_target):
        curr = start_id
        visited = set()
        while curr in resume_target:
            visited.add(curr)
            curr = resume_target[curr]
            if curr == start_id:
                problems.append(
                    f"continuation #{start_id} is part of a cyclic resume chain"
                )
                break
            if curr in visited:
                break

    for k in snap.continuations:
        if not k:
            continue
        where = f"continuation #{k.id}"
        if k.resume_throw != NULL_REF and k.resume_throw >= num_exns:
            problems.append(
                f"{where}: resume_throw names exception #{k.resume_throw}, not in the snapshot"
            )
        if k.state == CONT_FINISHED:
            if k.bound_args_count:
                problems.append(f"{where}: finished continuation has bound arguments")
            if k.resume_throw is not None and k.resume_throw != NULL_REF:
                problems.append(
                    f"{where}: finished continuation cannot carry a resume_throw exception"
                )
            if k.activations:
                problems.append(f"{where}: finished continuation has activations")
        elif k.state == CONT_ALLOCATED:
            if k.activations:
                problems.append(f"{where}: allocated continuation has activations")
            if len(k.args or []) != k.bound_args_count:
                problems.append(f"{where}: bound argument count does not match args")
        if k.activations is None:
            continue
        if not k.activations:
            problems.append(f"{where}: suspended, but in no function")
            continue
        # A return_call hands its frame to its callee, so the outermost function
        # need not be the one the continuation was entered with.
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
            if a.site == SITE_RESUME:
                if a.cont_id == 0:
                    problems.append(
                        f"{at}: a resume cannot target the root continuation"
                    )
                elif a.cont_id >= num_conts:
                    problems.append(
                        f"{at}: resumes continuation #{a.cont_id}, not in the snapshot"
                    )
            if a.site == SITE_OP and a.target_loop >= a.wasm_offset:
                problems.append(
                    f"{at}: goes back to a loop at +0x{a.target_loop:x},"
                    f" which does not open before its branch"
                )
            num_locals = snap.num_locals(a.func_index)
            if num_locals is not None and len(a.values) < num_locals:
                problems.append(
                    f"{at}: {len(a.values)} values, fewer than its {num_locals} locals"
                )
            for j, (type_id, word) in enumerate(a.values):
                if type_id not in VALUE_TYPES:
                    problems.append(f"{at} value {j}: unknown type id {type_id}")
                elif type_id in (VALTYPE_I32, VALTYPE_F32) and (
                    isinstance(word, int) and word >> 32
                ):
                    problems.append(
                        f"{at} value {j}: a 32-bit value with high bits set"
                    )
                check_ref(f"{at} value {j}", type_id, word)

    return problems


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

    manifest["format"]["section_order"] = snap.section_order
    for sec_id, body in snap.extra_sections.items():
        name = f"section{sec_id}.bin"
        with open(os.path.join(directory, name), "wb") as f:
            f.write(body)
        manifest["files"][f"section{sec_id}"] = name

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
                    site=ad["site_id"],
                    wasm_offset=ad["wasm_offset"],
                    target_loop=ad.get("target_loop"),
                    values=[(v["type_id"], v["word"]) for v in ad["values"]],
                    cont_id=ad.get("cont_id"),
                )
                for ad in d["activations"]
            ]
        snap.continuations.append(k)

    snap.host_state = blob("host") if "host" in files else b""

    snap.section_order = fmt.get("section_order", [])
    for key in files:
        if key.startswith("section"):
            snap.extra_sections[int(key[len("section") :])] = blob(key)

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


def _print_exception(snap, e):
    args = ", ".join(snap.describe_value(type_id, word) for type_id, word in e.args)
    print(f"  tag {e.tag_index:<3} ({args})" if args else f"  tag {e.tag_index:<3} ()")


def _print_memory(m):
    pages = f"{m.num_pages} page" + ("" if m.num_pages == 1 else "s")
    limit = f", max {m.max_pages}" if m.max_pages is not None else ""
    raw = sum(len(p) for kind, _, p in m.chunks if kind == CHUNK_RAW)
    fill = sum(p for kind, _, p in m.chunks if kind == CHUNK_FILL_FF)
    held = raw + fill

    size = f", {_human(m.size)}" if m.size is not None else ""
    print(f"  memory      {pages}{limit}{size}")
    if not m.chunks:
        print("              all zeroes")
        return

    saved = (1.0 - held / m.size) * 100.0 if m.size else 0.0
    print(
        f"              {len(m.chunks)} chunks hold {_human(held)}"
        f" ({saved:.1f}% left out as zeroes)"
    )


def _print_continuation(snap, k, verbose):
    kind = "root" if k.is_root else f"type {k.type_index}"
    print(
        f"  #{k.id:<3} {k.state_name:<10} {kind:<9} entry {snap.describe_func(k.entry_func_index)}"
    )
    if k.resume_throw != NULL_REF:
        print(f"         resumes by raising exception #{k.resume_throw}")
    if k.bound_args_count:
        print(f"         {k.bound_args_count} bound arguments")
    if not k.activations:
        return
    for i, a in enumerate(k.activations):
        where = snap.describe_offset(a.func_index, a.wasm_offset)
        extra = ""
        if a.site == SITE_OP:
            extra = f" -> loop@{snap.describe_offset(a.func_index, a.target_loop)}"
        elif a.site == SITE_RESUME:
            extra = f" -> runs continuation #{a.cont_id}"
        print(
            f"         {i:2d}  {snap.describe_func(a.func_index):<20} "
            f"at {a.site_name} {where}{extra}, {len(a.values)} values"
        )
        if verbose:
            _print_values(snap, a, "             ")


def cmd_info(args):
    snap = load(args.file, module=args.wasm)
    file_path, _ = _parse_wasm_path(args.file)
    size = os.path.getsize(file_path) if os.path.exists(file_path) else 0

    print(f"{args.file}  {_human(size)}")
    kind = "postmortem" if snap.is_postmortem else "resumable"
    print(f"  format      DMP version {snap.version}, flags 0x{snap.flags:x} ({kind})")
    if snap.timestamp_ms:
        import datetime

        when = datetime.datetime.fromtimestamp(
            snap.timestamp_ms / 1000.0, tz=datetime.timezone.utc
        )
        print(f"  taken       {when.isoformat(timespec='milliseconds')}")
    print(f"  module      {snap.module_hash:016x}")

    if snap.continuations:
        print()
        print(f"  continuations ({len(snap.continuations)}):")
        for k in snap.continuations:
            if k:
                _print_continuation(snap, k, args.values)

    if snap.exceptions:
        print()
        print(f"  exceptions ({len(snap.exceptions)}):")
        for e in snap.exceptions:
            if e:
                _print_exception(snap, e)

    if snap.memories:
        print()
        for m in snap.memories:
            _print_memory(m)

    if snap.globals:
        print(f"  globals     {len(snap.globals)}")
        for i, (gt, gv) in enumerate(snap.globals):
            name = TYPE_NAMES.get(gt, f"type{gt}")
            print(f"    {i:2d}  {name:<10} {snap.describe_value(gt, gv)}")

    for i, (type_id, elems) in enumerate(snap.tables):
        non_null = sum(1 for e in elems if e != NULL_REF)
        print(
            f"  table {i:<5} {TYPE_NAMES.get(type_id, f'type{type_id}')}, "
            f"{len(elems)} elements, {non_null} non-null"
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


def _repack_complaint(original, repacked):
    """Says where a repack first went wrong, which is where to go looking."""
    limit = min(len(original), len(repacked))
    at = next((i for i in range(limit) if original[i] != repacked[i]), limit)

    if at < limit:
        return (
            f"does not survive a round trip: byte {at} is "
            f"0x{original[at]:02x} but was written back as 0x{repacked[at]:02x}"
        )
    return (
        f"does not survive a round trip: {len(original)} bytes in, "
        f"{len(repacked)} bytes out, agreeing up to byte {at}"
    )


def cmd_verify(args):
    snap = load(args.file, module=args.wasm)
    problems = verify(snap)

    # Reading it and writing it back has to give the same bytes. This is what
    # holds this tool to wasm3's own writer: a field one of them emits and the
    # other does not shows up here and nowhere else.
    if snap.raw:
        try:
            repacked = pack(snap)
        except Exception as e:  # noqa: BLE001 - any failure is a complaint
            problems.append(f"cannot be written back: {e}")
        else:
            if repacked != snap.raw:
                problems.append(_repack_complaint(snap.raw, repacked))

    if not problems:
        print(f"{args.file}: ok")
        return 0
    for p in problems:
        print(f"{args.file}: {p}")
    return 1


def cmd_embed(args):
    wasm_path = args.wasm
    out_path = args.output
    name = None

    if out_path:
        out_file, out_name = _parse_wasm_path(out_path)
        if out_name is not None:
            out_path = out_file
            name = out_name

    if wasm_path:
        wasm_file, wasm_name = _parse_wasm_path(wasm_path)
        if wasm_name is not None:
            wasm_path = wasm_file
            name = wasm_name

    if not wasm_path and not out_path:
        raise FormatError("either --wasm or --output must be specified")
    if not wasm_path:
        wasm_path = out_path
    if not out_path:
        out_path = wasm_path

    with open(args.snapshot, "rb") as f:
        snap_data = f.read()
    # Verify snapshot bytes are valid
    load(snap_data)
    with open(wasm_path, "rb") as f:
        wasm_data = f.read()
    out_data = embed_snapshot_in_wasm(wasm_data, snap_data, name)
    with open(out_path, "wb") as f:
        f.write(out_data)
    name_str = f" as '{name}'" if name else ""
    print(
        f"embedded {args.snapshot} into {out_path}{name_str} ({_human(len(out_data))})"
    )
    return 0


def cmd_extract(args):
    wasm_path, name = _parse_wasm_path(args.wasm)

    with open(wasm_path, "rb") as f:
        wasm_data = f.read()
    payload = _select_snapshot(extract_snapshots_from_wasm(wasm_data), name, wasm_path)
    out_path = args.output
    if not out_path:
        out_path = f"{name}.dmp" if name else "snapshot.dmp"
    with open(out_path, "wb") as f:
        f.write(MAGIC + struct.pack("<I", 1) + payload)
    print(f"extracted snapshot to {out_path} ({_human(len(payload))})")
    return 0


def main(argv=None):
    wasm_help = (
        "the module the snapshot belongs to: function names, locals, module offsets"
    )

    parser = argparse.ArgumentParser(
        description="Read, inspect and rebuild wasm3 snapshots (.dmp / .wasm)"
    )
    parser.add_argument("--wasm", help=wasm_help)

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
        "embed", help="embed a snapshot into a wasm module as a custom section"
    )
    p.add_argument("snapshot", help="input snapshot (.dmp)")
    p.add_argument(
        "--wasm", "-w", default=None, help="input wasm module (supports .wasm:<name>)"
    )
    p.add_argument(
        "-o",
        "--output",
        default=None,
        help="output wasm module (supports .wasm:<name>)",
    )
    p.set_defaults(func=cmd_embed)

    p = sub.add_parser(
        "extract", help="extract a snapshot from a wasm module's custom section"
    )
    p.add_argument("wasm", help="input wasm module (supports .wasm:<name>)")
    p.add_argument(
        "-o",
        "--output",
        default=None,
        help="output snapshot (.dmp) (default: <name>.dmp or snapshot.dmp)",
    )
    p.set_defaults(func=cmd_extract)

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
