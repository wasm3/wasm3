#!/usr/bin/env python3
"""Read, inspect and rebuild wasm3 snapshots (.w3s).

A snapshot holds the execution state of a suspended runtime: linear memory,
globals, tables, the value stack and the frames that were standing when it
suspended. `m3_SaveSnapshot` writes it and `m3_LoadSnapshot` reads it back.

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
    print(snap.globals, snap.frames)
    snap.memories[0].data          # bytes, expanded from the chunk encoding
    open("out.w3s", "wb").write(w3s.pack(snap))

Version 1 of the format records the program counter as an offset into compiled
metacode and the value stack as untyped slots, so a snapshot is only meaningful
to a build that matches the one that wrote it: same wasm3 revision, same slot
width, same endianness, and the same answer for whether gas metering was on.
Neither the slot width nor the wasm3 revision is written down. This reader
tries both endiannesses and slot widths, keeping the one that accounts for
every byte.
"""

import argparse
import json
import os
import struct
import sys

MAGIC = b"W3S"
SUPPORTED_VERSIONS = (1,)

CHUNK_END = 0x00
CHUNK_RAW = 0x01
CHUNK_FILL_FF = 0x02

FLAG_POSTMORTEM = 0x1

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

# M3FrameKind, in the order m3_env.h declares it. The tail of the list is
# conditional on the build, so anything past frame_try is only a guess.
FRAME_KINDS = {0: "call", 1: "loop", 2: "try", 3: "entry", 4: "resume"}

TRAILER_FIXED = 4 + 4 + 4 + 8 + 8 + 4  # entry, current, pc, r0, fp0, numFrames
FRAME_SIZE = 1 + 4 + 4 + 4 + 8 + 8 + 4 + 4 + 1


class FormatError(Exception):
    pass


# --------------------------------------------------------------------------- model


class Memory:
    __slots__ = ("_data", "chunks", "max_pages", "num_pages", "page_size")

    def __init__(self, num_pages, max_pages, chunks, page_size):
        self.num_pages = num_pages
        self.max_pages = max_pages
        self.page_size = page_size
        self.chunks = chunks  # list of (kind, offset, payload-or-length)
        self._data = None

    @property
    def size(self):
        return self.num_pages * self.page_size

    @property
    def data(self):
        """The linear memory, expanded. Always in Wasm byte order."""
        if self._data is None:
            buf = bytearray(self.size)
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
        return {
            "pages": self.num_pages,
            "max_pages": self.max_pages,
            "page_size": self.page_size,
            "bytes": self.size,
            "stored_bytes": raw,
            "filled_ff_bytes": ff,
            "implicit_zero_bytes": self.size - raw - ff,
            "chunks": len(self.chunks),
        }


class Frame:
    __slots__ = (
        "callee_index",
        "fp0",
        "func_index",
        "handlers_live",
        "kind",
        "num_clauses",
        "pc_offset",
        "r0",
        "sp_slot",
    )

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw[k])

    @property
    def kind_name(self):
        return FRAME_KINDS.get(self.kind, f"kind{self.kind}")

    def to_dict(self):
        return {k: getattr(self, k) for k in self.__slots__}


class Snapshot:
    """Everything a .w3s file holds, plus how it was encoded."""

    def __init__(self):
        self.version = 1
        self.flags = 0
        self.endian = "<"
        self.slot_size = 4
        self.memories = []
        self.globals = []  # list of (type, raw u64)
        self.tables = []  # list of list of func index, -1 for null
        self.sp_slot = 0
        self.stack = b""
        self.entry_func_index = 0
        self.current_func_index = 0
        self.pc_offset = 0
        self.r0 = 0
        self.fp0 = 0.0
        self.frames = []  # outermost first, as written
        self.names = {}  # func index -> name, if a module was given

    # ---- convenience

    @property
    def endian_name(self):
        return "little" if self.endian == "<" else "big"

    @property
    def is_postmortem(self):
        return bool(self.flags & FLAG_POSTMORTEM)

    @property
    def num_slots(self):
        return len(self.stack) // self.slot_size

    def slots(self):
        """The value stack as integers. Untyped - version 1 records no types."""
        fmt = "I" if self.slot_size == 4 else "Q"
        return list(struct.unpack(f"{self.endian}{self.num_slots}{fmt}", self.stack))

    def func_name(self, index):
        return self.names.get(index)

    def describe_func(self, index):
        name = self.func_name(index)
        return f"{index} ({name})" if name else f"{index}"

    def to_dict(self, *, include_stack=False):
        d = {
            "format": {
                "magic": "W3S",
                "version": self.version,
                "flags": self.flags,
                "postmortem": self.is_postmortem,
                "endian": self.endian_name,
                "slot_size": self.slot_size,
            },
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
                    "size": len(t),
                    "elements": t,
                    "non_null": sum(1 for e in t if e >= 0),
                }
                for i, t in enumerate(self.tables)
            ],
            "execution": {
                "entry_func_index": self.entry_func_index,
                "entry_func_name": self.func_name(self.entry_func_index),
                "current_func_index": self.current_func_index,
                "current_func_name": self.func_name(self.current_func_index),
                "pc_offset_words": self.pc_offset,
                "r0": self.r0,
                "fp0": self.fp0,
                "sp_slot": self.sp_slot,
                "saved_slots": self.num_slots,
            },
            "frames": [
                dict(
                    f.to_dict(),
                    kind_name=f.kind_name,
                    func_name=self.func_name(f.func_index),
                )
                for f in self.frames
            ],
        }
        if include_stack:
            d["stack_slots"] = self.slots()
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
        if self.pos + n > len(self.data):
            raise FormatError(
                f"truncated: wanted {n} bytes at offset {self.pos}, "
                f"{len(self.data) - self.pos} remain"
            )
        out = self.data[self.pos : self.pos + n]
        self.pos += n
        return out

    def u8(self):
        return self.take(1)[0]

    def u32(self):
        return struct.unpack(f"{self.endian}I", self.take(4))[0]

    def i32(self):
        return struct.unpack(f"{self.endian}i", self.take(4))[0]

    def u64(self):
        return struct.unpack(f"{self.endian}Q", self.take(8))[0]

    def f64(self):
        return struct.unpack(f"{self.endian}d", self.take(8))[0]

    @property
    def remaining(self):
        return len(self.data) - self.pos


def _read_header(data):
    if len(data) < 4:
        raise FormatError("too short to be a snapshot")
    if data[:3] != MAGIC:
        raise FormatError(f"not a W3S snapshot (magic is {data[:3].hex()})")
    return data[3]


def _parse(data, endian, slot_size):
    c = _Cursor(data[4:], endian)
    s = Snapshot()
    s.endian = endian
    s.slot_size = slot_size

    s.version = _read_header(data)
    if s.version not in SUPPORTED_VERSIONS:
        raise FormatError(f"unsupported version {s.version}")
    s.flags = c.u32()

    for _ in range(_sane(c.u32(), "memories")):
        num_pages = c.u64()
        max_pages = c.u64()
        page_size = c.u32()
        chunks = []
        while True:
            kind = c.u8()
            if kind == CHUNK_END:
                break
            if kind == CHUNK_RAW:
                off, ln = c.u32(), c.u32()
                chunks.append((kind, off, c.take(ln)))
            elif kind == CHUNK_FILL_FF:
                off, ln = c.u32(), c.u32()
                chunks.append((kind, off, ln))
            else:
                raise FormatError(
                    f"unknown memory chunk type 0x{kind:02x} at offset {c.pos - 1}"
                )
        s.memories.append(Memory(num_pages, max_pages, chunks, page_size))

    for _ in range(_sane(c.u32(), "globals")):
        s.globals.append((c.u8(), c.u64()))

    for _ in range(_sane(c.u32(), "tables")):
        s.tables.append([c.i32() for _ in range(_sane(c.u32(), "table elements"))])

    s.sp_slot = c.u32()
    num_slots = c.u32()
    s.stack = c.take(num_slots * slot_size)

    s.entry_func_index = c.u32()
    s.current_func_index = c.u32()
    s.pc_offset = c.u32()
    s.r0 = c.u64()
    s.fp0 = c.f64()

    for _ in range(_sane(c.u32(), "frames")):
        s.frames.append(
            Frame(
                kind=c.u8(),
                sp_slot=c.u32(),
                func_index=c.u32(),
                pc_offset=c.u32(),
                r0=c.u64(),
                fp0=c.f64(),
                callee_index=c.u32(),
                num_clauses=c.u32(),
                handlers_live=c.u8(),
            )
        )

    if c.remaining:
        raise FormatError(f"{c.remaining} trailing bytes")

    return s


def _sane(count, what, limit=1 << 24):
    if count > limit:
        raise FormatError(f"implausible {what} count: {count}")
    return count


def load(source, *, slot_size=None, module=None):
    """Read a snapshot from a path, a file object or bytes.

    slot_size is 4 or 8; when omitted both are tried and the one that accounts
    for the whole file wins. module is a path to the .wasm the snapshot belongs
    to, used only to put names to function indices.
    """
    if isinstance(source, (bytes, bytearray)):
        data = bytes(source)
    elif hasattr(source, "read"):
        data = source.read()
    else:
        with open(source, "rb") as f:
            data = f.read()

    version = _read_header(data)
    if version not in SUPPORTED_VERSIONS:
        raise FormatError(f"unsupported version {version}")
    candidates = [slot_size] if slot_size else [4, 8]

    errors = []
    for endian in ("<", ">"):
        endian_name = "little" if endian == "<" else "big"
        for size in candidates:
            try:
                snap = _parse(data, endian, size)
            except FormatError as e:
                errors.append(f"{endian_name}-endian, slot_size={size}: {e}")
                continue
            if module:
                snap.names = read_function_names(module)
            return snap

    error_list = "\n  ".join(errors)
    raise FormatError(f"could not parse snapshot:\n  {error_list}")


# --------------------------------------------------------------------------- writing


def pack(snap):
    """Serialize a Snapshot back to bytes, byte-for-byte with what wasm3 wrote."""
    e = snap.endian
    out = bytearray()

    def u8(v):
        out.append(v & 0xFF)

    def u32(v):
        out.extend(struct.pack(f"{e}I", v & 0xFFFFFFFF))

    def i32(v):
        out.extend(struct.pack(f"{e}i", v))

    def u64(v):
        out.extend(struct.pack(f"{e}Q", v & 0xFFFFFFFFFFFFFFFF))

    def f64(v):
        out.extend(struct.pack(f"{e}d", v))

    out.extend(MAGIC)
    u8(snap.version)
    u32(snap.flags)

    u32(len(snap.memories))
    for m in snap.memories:
        u64(m.num_pages)
        u64(m.max_pages)
        u32(m.page_size)
        for kind, off, payload in m.chunks:
            u8(kind)
            u32(off)
            if kind == CHUNK_RAW:
                u32(len(payload))
                out.extend(payload)
            else:
                u32(payload)
        u8(CHUNK_END)

    u32(len(snap.globals))
    for type_id, value in snap.globals:
        u8(type_id)
        u64(value)

    u32(len(snap.tables))
    for table in snap.tables:
        u32(len(table))
        for elem in table:
            i32(elem)

    u32(snap.sp_slot)
    u32(snap.num_slots)
    out.extend(snap.stack)

    u32(snap.entry_func_index)
    u32(snap.current_func_index)
    u32(snap.pc_offset)
    u64(snap.r0)
    f64(snap.fp0)

    u32(len(snap.frames))
    for f in snap.frames:
        u8(f.kind)
        u32(f.sp_slot)
        u32(f.func_index)
        u32(f.pc_offset)
        u64(f.r0)
        f64(f.fp0)
        u32(f.callee_index)
        u32(f.num_clauses)
        u8(f.handlers_live)

    return bytes(out)


def encode_memory(data, run_threshold=128):
    """Chunk-encode linear memory the way StreamWriteMemoryChunks does.

    Runs of 0x00 at least run_threshold long are dropped (memory starts zeroed);
    runs of 0xFF that long become a fill. Everything else is stored raw.
    """
    chunks = []
    n = len(data)
    i = 0
    while i < n:
        b = data[i]
        if b in (0x00, 0xFF):
            run = 1
            while i + run < n and data[i + run] == b:
                run += 1
            if run >= run_threshold:
                if b == 0xFF:
                    chunks.append((CHUNK_FILL_FF, i, run))
                i += run
                continue
        start = i
        while i < n:
            b = data[i]
            if b in (0x00, 0xFF):
                run = 1
                while i + run < n and data[i + run] == b:
                    run += 1
                if run >= run_threshold:
                    break
                i += run
            else:
                i += 1
        if i > start:
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
    imported_funcs = 0
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

        if section_id == 2:  # imports
            count, p = leb(pos)
            for _ in range(count):
                mlen, p = leb(p)
                p += mlen
                nlen, p = leb(p)
                p += nlen
                kind = data[p]
                p += 1
                if kind == 0x00:
                    imported_funcs += 1
                    _, p = leb(p)
                elif kind == 0x01:  # table
                    p += 1
                    limits = data[p]
                    p += 1
                    _, p = leb(p)
                    if limits & 0x01:
                        _, p = leb(p)
                elif kind == 0x02:  # memory
                    limits = data[p]
                    p += 1
                    _, p = leb(p)
                    if limits & 0x01:
                        _, p = leb(p)
                elif kind == 0x03:  # global
                    p += 2
                else:  # tag
                    p += 1
                    _, p = leb(p)

        elif section_id == 0:  # custom
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
    _ = imported_funcs

    for idx, name in exported.items():
        names.setdefault(idx, name)

    return names


# --------------------------------------------------------------------------- checks


def verify(snap):
    """Structural complaints about a snapshot. Empty list means nothing found."""
    problems = []

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

    for i, (type_id, _) in enumerate(snap.globals):
        if type_id not in TYPE_NAMES:
            problems.append(f"global {i}: unknown type id {type_id}")

    if snap.sp_slot > snap.num_slots:
        problems.append(
            f"sp is slot {snap.sp_slot} but only {snap.num_slots} slots were saved"
        )

    for i, f in enumerate(snap.frames):
        if f.kind not in FRAME_KINDS:
            problems.append(f"frame {i}: unknown kind {f.kind}")
        if f.sp_slot > snap.num_slots:
            problems.append(
                f"frame {i}: sp is slot {f.sp_slot} but only {snap.num_slots} slots were saved"
            )
        if f.kind == 4:
            problems.append(
                f"frame {i}: a captured resume, which version 1 cannot "
                "represent - the file is probably corrupt"
            )

    if snap.names:
        highest = max(snap.names)
        for label, idx in (
            ("entry", snap.entry_func_index),
            ("current", snap.current_func_index),
        ):
            if idx > highest:
                problems.append(
                    f"{label} function index {idx} is past the last function "
                    f"in the module ({highest})"
                )

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
    out["execution"] = {
        "current_func_index": [a.current_func_index, b.current_func_index],
        "pc_offset_words": [a.pc_offset, b.pc_offset],
        "frame_depth": [len(a.frames), len(b.frames)],
    }
    return out


# --------------------------------------------------------------------------- unpack / pack


def unpack(snap, directory):
    """Write a snapshot as snapshot.json plus one binary per blob."""
    os.makedirs(directory, exist_ok=True)

    manifest = snap.to_dict()
    manifest["files"] = {}

    for i, m in enumerate(snap.memories):
        name = f"memory{i}.bin"
        with open(os.path.join(directory, name), "wb") as f:
            f.write(m.data)
        manifest["files"][f"memory{i}"] = name

    with open(os.path.join(directory, "stack.bin"), "wb") as f:
        f.write(snap.stack)
    manifest["files"]["stack"] = "stack.bin"

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
    snap.slot_size = fmt["slot_size"]

    files = manifest.get("files", {})

    for i, m in enumerate(manifest["memories"]):
        with open(os.path.join(directory, files[f"memory{i}"]), "rb") as f:
            data = f.read()
        snap.memories.append(
            Memory(
                m["pages"],
                m["max_pages"],
                encode_memory(data, run_threshold),
                m["page_size"],
            )
        )

    snap.globals = [(g["type_id"], g["value"]) for g in manifest["globals"]]
    snap.tables = [t["elements"] for t in manifest["tables"]]

    with open(os.path.join(directory, files["stack"]), "rb") as f:
        snap.stack = f.read()

    ex = manifest["execution"]
    snap.sp_slot = ex["sp_slot"]
    snap.entry_func_index = ex["entry_func_index"]
    snap.current_func_index = ex["current_func_index"]
    snap.pc_offset = ex["pc_offset_words"]
    snap.r0 = ex["r0"]
    snap.fp0 = ex["fp0"]

    snap.frames = [
        Frame(
            kind=f["kind"],
            sp_slot=f["sp_slot"],
            func_index=f["func_index"],
            pc_offset=f["pc_offset"],
            r0=f["r0"],
            fp0=f["fp0"],
            callee_index=f["callee_index"],
            num_clauses=f["num_clauses"],
            handlers_live=f["handlers_live"],
        )
        for f in manifest["frames"]
    ]
    return snap


# --------------------------------------------------------------------------- CLI


def _human(n):
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024 or unit == "GiB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024.0


def cmd_info(args):
    snap = load(args.file, slot_size=args.slot_size, module=args.wasm)
    size = os.path.getsize(args.file) if os.path.exists(args.file) else 0

    print(f"{args.file}  {_human(size)}")
    kind = "postmortem" if snap.is_postmortem else "resumable"
    print(f"  format      W3S version {snap.version}, flags 0x{snap.flags:x} ({kind})")
    print(f"  encoding    {snap.endian_name}-endian, {snap.slot_size}-byte slots")
    print()
    if snap.is_postmortem:
        print("  execution   unavailable after the trap")
    else:
        print(
            f"  suspended in function {snap.describe_func(snap.current_func_index)} "
            f"at metacode word {snap.pc_offset}"
        )
        print(f"  invoked     function {snap.describe_func(snap.entry_func_index)}")
        print(f"  registers   r0=0x{snap.r0:016x} fp0={snap.fp0!r}")
        print(f"  value stack {snap.num_slots} slots saved, sp at slot {snap.sp_slot}")

        print()
        if snap.frames:
            print("  frames (outermost first):")
            for i, f in enumerate(snap.frames):
                extra = ""
                if f.kind_name == "call" and f.callee_index:
                    extra = f" -> calls {snap.describe_func(f.callee_index)}"
                elif f.kind_name == "try":
                    extra = f" {f.num_clauses} clauses, handlers {'live' if f.handlers_live else 'retired'}"
                print(
                    f"    {i:2d}  {f.kind_name:<6} in "
                    f"{snap.describe_func(f.func_index):<20} word {f.pc_offset:<6d} "
                    f"sp slot {f.sp_slot:<6d}{extra}"
                )
        else:
            print("  frames      none - suspended in the entry function itself")

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
                f"{_as_signed(value, type_id)}"
            )

    for i, table in enumerate(snap.tables):
        print(
            f"  table {i}     {len(table)} elements, "
            f"{sum(1 for e in table if e >= 0)} non-null"
        )

    problems = verify(snap)
    if problems:
        print()
        print("  problems:")
        for p in problems:
            print(f"    - {p}")

    return 1 if problems else 0


def cmd_unpack(args):
    snap = load(args.file, slot_size=args.slot_size, module=args.wasm)
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
    snap = load(args.file, slot_size=args.slot_size, module=args.wasm)
    problems = verify(snap)
    if not problems:
        print(f"{args.file}: ok")
        return 0
    for p in problems:
        print(f"{args.file}: {p}")
    return 1


def cmd_diff(args):
    a = load(args.before, slot_size=args.slot_size, module=args.wasm)
    b = load(args.after, slot_size=args.slot_size, module=args.wasm)
    print(json.dumps(diff(a, b), indent=2))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Read, inspect and rebuild wasm3 snapshots (.w3s)"
    )
    parser.add_argument(
        "--slot-size",
        type=int,
        choices=(4, 8),
        help="value stack slot width; detected when omitted",
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
