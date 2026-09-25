#!/usr/bin/env python3
"""Snapshot wire-format and CLI regressions; uses the tree's Wasm WABT tools."""

import argparse
import contextlib
import copy
import importlib.util
import io
import json
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "snapshot_tool", ROOT / "extra/snapshot-tool.py"
)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)
spec = importlib.util.spec_from_file_location(
    "pause_points", ROOT / "extra/pause-points.py"
)
pause_points = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pause_points)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--exec", default=str(ROOT / "build/wasm3"))
parser.add_argument("--host", help="WASI-enabled Wasm3 to run the bundled assembler")
parser.add_argument(
    "--m3-test", help="the m3_test built with --exec (default: the one beside it)"
)
args, unittest_args = parser.parse_known_args()
EXEC = shlex.split(args.exec)
HOST = shlex.split(args.host) if args.host else EXEC
M3_TEST = (
    Path(args.m3_test)
    if args.m3_test
    else Path(EXEC[-1]).with_name("m3_test" + Path(EXEC[-1]).suffix)
)
HEADER = b"\0dmp\x01\0\0\0"


def leb(value):
    out = bytearray()
    while value >= 128:
        out.append((value & 127) | 128)
        value >>= 7
    out.append(value)
    return bytes(out)


def section(kind, body):
    return bytes([kind]) + leb(len(body)) + body


def sections(data):
    cursor = tool._Cursor(data[8:])
    result = []
    while cursor.remaining:
        kind = cursor.u8()
        result.append((kind, cursor.take(cursor.leb_u32())))
    return result


def custom(name, body):
    name = name.encode()
    return section(0, leb(len(name)) + name + body)


# The example frames in test/snapshot/frames, and the words they are written in
# (see the README there)
EXAMPLES = ROOT / "test/snapshot/frames"
EXAMPLE_SITES = {
    "back-edge": tool.SITE_OP,
    "suspend": tool.SITE_SUSPEND,
    "call": tool.SITE_CALL,
    "resume": tool.SITE_RESUME,
    "entry": tool.SITE_ENTRY,
}
EXAMPLE_TYPES = {
    "i32": tool.VALTYPE_I32,
    "i64": tool.VALTYPE_I64,
    "f32": tool.VALTYPE_F32,
    "f64": tool.VALTYPE_F64,
    "v128": tool.VALTYPE_V128,
    "funcref": tool.VALTYPE_FUNCREF,
    "externref": tool.VALTYPE_EXTERNREF,
    "exnref": tool.VALTYPE_EXNREF,
    "contref": tool.VALTYPE_CONTREF,
}


def example_expectations(source):
    """(pause, {continuation: [frame line, ...]}) out of an example's comments."""
    pause, frames = None, {}
    for line in source.read_text().splitlines():
        line = line.strip()
        if not line.startswith(";; @"):
            continue
        directive, _, rest = line[4:].partition(" ")
        if directive == "pause":
            pause = rest.strip()
        elif directive == "frame":
            cont, _, frame = rest.strip().partition(" ")
            frames.setdefault(cont, []).append(" ".join(frame.split()))
        else:
            raise ValueError(f"{source.name}: unknown directive @{directive}")
    return pause, frames


def example_frame(snap, a):
    """An activation, written the way an example writes one."""
    sites = {v: k for k, v in EXAMPLE_SITES.items()}
    types = {v: k for k, v in EXAMPLE_TYPES.items()}
    target = [f"loop@+0x{a.target_loop:x}"] if a.site == tool.SITE_OP else []
    num_locals = snap.num_locals(a.func_index)
    values = [types.get(t, hex(t)) for t, _ in a.values]
    return " ".join(
        [str(a.func_index), sites.get(a.site, f"site{a.site}")]
        + target
        + [":"]
        + values[:num_locals]
        + ["|"]
        + values[num_locals:]
    )


def example_continuation(snap, selector):
    root = snap.continuations[0]
    if selector == "root":
        return root
    if selector == "target":
        return snap.continuations[root.activations[-1].cont_id]
    if selector.startswith("global:"):
        return snap.continuations[snap.globals[int(selector[7:])][1]]
    raise ValueError(f"unknown continuation {selector}")


class SnapshotFormatTests(unittest.TestCase):
    @classmethod
    def command(cls, *options, executable=None):
        options = [
            (
                p.relative_to(ROOT).as_posix()
                if isinstance(p, Path) and p.is_relative_to(ROOT)
                else str(p)
            )
            for p in options
        ]
        return subprocess.run(
            (executable or EXEC) + options,
            capture_output=True,
            timeout=30,
            cwd=ROOT,
        )

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(
            prefix="snapshot-format-", dir=ROOT / "test"
        )
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.module = cls.directory / "input.wasm"
        result = cls.command(
            "--stack-size",
            "1048576",
            ROOT / "test/wasi/wabt/wat2wasm.wasm",
            "--enable-all",
            ROOT / "test/snapshot/format.wat",
            "-o",
            cls.module,
            executable=HOST,
        )
        if result.returncode:
            raise RuntimeError(result.stderr.decode())
        cls.snapshot = cls.directory / "checkpoint.dmp"
        result = cls.command("--gas-limit", "1", "--snapshot", cls.snapshot, cls.module)
        if result.returncode or not cls.snapshot.exists():
            raise RuntimeError(result.stderr.decode())
        cls.raw = cls.snapshot.read_bytes()
        cls.parts = sections(cls.raw)
        cls.wasm = cls.module.read_bytes()
        cls.fixture_samples = {}

    def write(self, name, data):
        path = self.directory / name
        path.write_bytes(data)
        return path

    def replace(self, kind, body):
        return HEADER + b"".join(
            section(k, body if k == kind else b) for k, b in self.parts
        )

    def reject(self, data, python=True):
        if python:
            with self.assertRaises(tool.FormatError):
                tool.load(data)
        result = self.command("--resume", self.write("bad.dmp", data), self.module)
        self.assertNotEqual(result.returncode, 0, result.stderr.decode())
        self.assertIn(b"failed loading snapshot", result.stderr)

    def test_container_and_meta(self):
        self.assertTrue(self.raw.startswith(HEADER))
        snapshot = tool.load(self.raw)
        meta = tool._Cursor(dict(self.parts)[0])
        self.assertEqual(meta.leb_u32(), 0)  # flags; the section opens with them
        meta.leb_u64()  # timestamp
        module_hash = meta.leb_u64()
        self.assertEqual(meta.leb_u32(), 1)  # root; no engine hash between these fields
        self.assertEqual(meta.leb_u32(), 0)
        self.assertEqual(meta.leb_u64(), sum(m.size for m in snapshot.memories))
        self.assertEqual(meta.leb_u64(), sum(len(e) for _, e in snapshot.tables))
        self.assertEqual(meta.leb_u32(), 0)
        self.assertEqual(meta.remaining, 0)
        self.assertEqual(module_hash, tool.module_hash(self.wasm))
        self.assertEqual(tool.pack(tool.load(self.raw)), self.raw)

    def test_resource_cli_arguments(self):
        for flag, values in (
            ("--gas-limit", ("abc", "nan", "inf", "-1", "-0", "1tail")),
            (
                "--max-memory",
                ("-1", "1MB", "1.5M", "18446744073709551616", "18446744073709551615G"),
            ),
            ("--max-table-elements", ("-1", "1K", "1.5", "abc")),
            ("--max-continuations", ("-1", "1K", "abc")),
        ):
            for value in values:
                with self.subTest(flag=flag, value=value):
                    result = self.command(flag, value, self.module)
                    self.assertEqual(result.returncode, 1)
                    self.assertIn(b"--max-memory", result.stdout)

        empty_module = self.write("empty.wasm", b"\x00asm\x01\x00\x00\x00")
        for flag, value in (
            ("--gas-limit", "1e-3"),
            ("--gas-limit", "1e300"),
            ("--max-memory", "64k"),
            ("--max-table-elements", "0"),
        ):
            with self.subTest(flag=flag, value=value):
                result = self.command(flag, value, "--validate-only", empty_module)
                self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_embedded_resume_skips_start(self):
        embedded = tool.embed_snapshot_in_wasm(self.wasm, self.raw)
        self.assertEqual(tool.extract_snapshots_from_wasm(embedded)[""], self.raw[8:])
        path = self.write("embedded.wasm", embedded)
        result = self.command(path)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertNotIn(b"snapshot-start", result.stdout + result.stderr)
        result = self.command("--resume", "none", path)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertIn(b"snapshot-start", result.stdout + result.stderr)
        result = self.command("--resume", path)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertNotIn(b"snapshot-start", result.stdout + result.stderr)
        # the .wasm on --resume is the module to run, so there is no other
        result = self.command("--resume", path, self.module)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"is the module to run", result.stderr)

    def test_engine_embeds_a_section_stream(self):
        path = self.directory / "engine.wasm"
        result = self.command("--gas-limit", "1", "--snapshot", path, self.module)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        payload = tool.extract_snapshots_from_wasm(path.read_bytes())[""]
        self.assertFalse(payload.startswith(b"\0dmp"))
        self.assertEqual(payload[0], 0)
        self.assertEqual(
            tool.load(path.read_bytes()).module_hash, tool.load(self.raw).module_hash
        )
        result = self.command(path)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertNotIn(b"snapshot-start", result.stdout + result.stderr)

    def test_custom_metadata_does_not_change_identity(self):
        path = self.write(
            "metadata.wasm", self.wasm + custom("producer", b"changed metadata")
        )
        result = self.command("--resume", self.snapshot, path)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        data = bytearray(self.wasm)
        data[data.index(b"snapshot-start")] = ord(
            "S"
        )  # changes a standard data section
        path = self.write("changed.wasm", data)
        result = self.command("--resume", self.snapshot, path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"different module", result.stderr)

    def test_duplicate_checkpoints(self):
        for name in (None, "checkpoint"):
            with self.subTest(name=name):
                first = tool.embed_snapshot_in_wasm(self.wasm, self.raw, name)
                second = tool.embed_snapshot_in_wasm(first, self.raw, name)
                self.assertEqual(first, second)
                full_name = "snapshot" if name is None else f"snapshot.{name}"
                duplicate = first + custom(full_name, self.raw[8:])
                with self.assertRaises(tool.FormatError):
                    tool.load(duplicate)
                # a custom section does not make the module invalid: only
                # selecting the name the two share fails
                path = self.write("duplicate.wasm", duplicate)
                result = self.command("--validate-only", path)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                selected = path.relative_to(ROOT).as_posix()
                if name is not None:
                    selected += f":{name}"
                result = self.command(selected)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"duplicate embedded snapshot name", result.stderr)
                result = self.command("--resume", "none", path)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                # selecting another name that is unique succeeds
                with_other = duplicate + custom("snapshot.other", self.raw[8:])
                self.assertEqual(
                    tool.load(with_other, name="other").module_hash,
                    tool.load(self.raw).module_hash,
                )

    def test_selecting_an_embedded_snapshot(self):
        named = tool.embed_snapshot_in_wasm(self.wasm, self.raw, "only")
        # no name is the unnamed default, which this module does not have
        with self.assertRaises(tool.FormatError):
            tool.load(named)
        self.assertEqual(
            tool.load(named, name="only").module_hash, tool.load(self.raw).module_hash
        )
        self.assertEqual(
            tool._parse_wasm_path("dir.wasm:x/app.wasm:name"),
            ("dir.wasm:x/app.wasm", "name"),
        )

    def test_embedded_snapshot_belongs_to_its_module(self):
        other = bytearray(self.wasm)
        other[other.index(b"snapshot-start")] = ord("S")
        with self.assertRaises(tool.FormatError):
            tool.embed_snapshot_in_wasm(bytes(other), self.raw)
        embedded = self.write(
            "carrier.wasm", tool.embed_snapshot_in_wasm(self.wasm, self.raw)
        )
        tool.load(str(embedded), module=str(embedded))
        # read against another module, even an identical one, it is refused
        with self.assertRaises(tool.FormatError):
            tool.load(str(embedded), module=str(self.module))

    def test_postmortem_is_not_embedded(self):
        snapshot = tool.Snapshot()
        snapshot.flags = 1
        with self.assertRaises(tool.FormatError):
            tool.embed_snapshot_in_wasm(self.wasm, tool.pack(snapshot))

    def test_section_framing(self):
        for kind, body in self.parts:
            with self.subTest(kind=kind, mutation="extra byte"):
                self.reject(self.replace(kind, body + b"X"))
            with self.subTest(kind=kind, mutation="short body"):
                self.reject(self.replace(kind, body[:-1]))
        self.reject(self.raw + section(0, self.parts[0][1]))
        self.reject(self.raw + section(255, b"x") * 2)
        self.reject(HEADER + section(255, b"x") + self.raw[8:])
        self.reject(self.raw + section(255, b""))
        valid = self.raw + section(255, b"opaque extension")
        self.assertEqual(tool.pack(tool.load(valid)), valid)
        # and where it stands, not just that it is there
        moved = HEADER + b"".join(
            section(k, b) + (section(200, b"between") if k == 0 else b"")
            for k, b in self.parts
        )
        self.assertEqual(tool.pack(tool.load(moved)), moved)
        result = self.command(
            "--resume", self.write("extension.dmp", valid), self.module
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_host_state_is_the_whole_section(self):
        snapshot = tool.load(self.raw)
        snapshot.host_state = b"host"
        raw = tool.pack(snapshot)
        self.assertEqual(dict(sections(raw))[7], b"host")
        self.assertEqual(tool.load(raw).host_state, b"host")
        # the CLI sets no hooks, so nothing here can take the embedder's bytes
        self.reject(raw, python=False)
        result = self.command("--resume", self.write("host.dmp", raw), self.module)
        self.assertIn(b"nothing here restores it", result.stderr)

    def test_page_size_is_in_the_file(self):
        memory = tool.load(self.raw).memories[0]
        self.assertEqual((memory.page_size, memory.max_pages), (65536, None))
        memory = tool.load(self.raw, module=str(self.module)).memories[0]
        self.assertEqual(
            (memory.page_size, memory.max_pages, memory.size), (65536, 1, 65536)
        )

        # read without the module, a page of one byte is still one byte
        snap = tool.load(self.raw)
        snap.memories[0].page_size = 1
        snap.memories[0].num_pages = 16
        path = self.write("byte-pages.dmp", tool.pack(snap))
        snap = tool.load(path.read_bytes())
        self.assertEqual((snap.memories[0].size, snap.total_memory_bytes), (16, 16))
        self.assertEqual(snap.memories[0].data[:14], b"snapshot-start")
        self.assertEqual(tool.verify(snap), [])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = tool.main(["verify", str(path)])
        self.assertEqual(status, 0, output.getvalue())
        # and read with it, the module's page size is the one that counts
        self.assertIn(
            "memory 0: page size 1, but the module declares 65536",
            tool.verify(tool.load(str(path), module=str(self.module))),
        )

    def test_byte_pages_round_trip(self):
        # A guarded memory is backed a system page at a time, so a build with
        # them refuses a memory of one-byte pages outright
        if b"guarded-mem" in self.command("--version").stdout:
            self.skipTest("this build refuses pages smaller than the system's")
        module = self.assemble(ROOT / "test/snapshot/page-size.wat")
        path = self.directory / "page-size.dmp"
        result = self.command("--gas-limit", "1", "--snapshot", path, module)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        raw = path.read_bytes()
        snap = tool.load(raw)
        memory = snap.memories[0]
        self.assertEqual((memory.page_size, memory.size), (1, 3))
        self.assertEqual(memory.data, b"abc")
        self.assertEqual(snap.total_memory_bytes, 3)
        self.assertEqual(tool.verify(snap), [])
        self.assertEqual(tool.pack(snap), raw)
        result = self.command("--resume", path, module)
        self.assertEqual(result.returncode, 0, result.stderr.decode())

        bad = copy.deepcopy(snap)
        bad.memories[0].page_size = 65536
        self.reject_snapshot(bad, module)

    def test_shared_memory_and_table_are_stored_once(self):
        # two imports of one memory, or one table, are one object at two
        # indices, and the file holds each once
        snapshot = tool.Snapshot()
        snapshot.flags = 1
        memory = tool.Memory(1, None, 65536, True, [(tool.CHUNK_RAW, 0, b"shared")])
        table = (tool.VALTYPE_FUNCREF, [tool.NULL_REF])
        snapshot.memories = [memory, memory]
        snapshot.tables = [table, table]
        raw = tool.pack(snapshot)
        bodies = dict(sections(raw))
        self.assertEqual(bodies[1][-2:], b"\x01\x00")  # memory 1 is memory 0
        self.assertEqual(bodies[2], b"\x02\x00\x00\x70\x01\x00\x01\x00")

        loaded = tool.load(raw)
        self.assertIs(loaded.memories[1], loaded.memories[0])
        self.assertIs(loaded.tables[1], loaded.tables[0])
        self.assertEqual(
            (loaded.total_memory_bytes, loaded.total_table_elements), (65536, 1)
        )
        self.assertEqual(tool.verify(loaded), [])
        self.assertEqual(tool.pack(loaded), raw)
        directory = self.directory / "unpacked-shared"
        tool.unpack(loaded, directory)
        self.assertEqual(tool.pack(tool.pack_directory(directory)), raw)

        # a record names itself, or one before it that names itself
        for body in (
            b"\x02\x00\x01\x70\x01\x00\x01\x00",
            b"\x03\x00\x00\x70\x01\x00\x01\x00\x02\x01",
        ):
            with self.subTest(body=body):
                with self.assertRaises(tool.FormatError):
                    tool.load(
                        HEADER
                        + b"".join(
                            section(k, body if k == 2 else b) for k, b in sections(raw)
                        )
                    )

    def test_info_summarizes_every_store_section(self):
        module, samples = self.samples("references.wat")
        for raw, wasm in (
            (self.raw, self.module),
            (tool.pack(samples[1]), module),
        ):
            path = self.write("info.dmp", raw)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                status = tool.main(["info", str(path), "--wasm", str(wasm)])
            self.assertEqual(status, 0, output.getvalue())
            self.assertIn("table 0", output.getvalue())

    def test_reserved_meta_flags(self):
        meta = self.parts[0][1]
        self.assertEqual(meta[0], 0)  # the section opens with the flags
        self.reject(self.replace(0, b"\x02" + meta[1:]))

    def test_store_records_are_in_index_order(self):
        body = dict(self.parts)[3]
        cursor = tool._Cursor(body)
        self.assertEqual(cursor.leb_u32(), 2)
        start = cursor.pos
        cursor.leb_u32()
        cursor.read_value()
        first = body[start : cursor.pos]
        # the same global twice: every index is in range, but global 1 is never
        # given a value and would resume with whatever instantiation left
        self.reject(self.replace(3, b"\x02" + first + first))

    def test_empty_sections_are_left_out(self):
        module, samples = self.samples("references.wat")
        raw = tool.pack(samples[1])
        kinds = [k for k, _ in sections(raw)]
        self.assertNotIn(1, kinds)  # the module has no memory
        self.assertNotIn(7, kinds)  # and the CLI no host state
        # present with nothing in it is as wrong as absent with something
        with_empty = raw + section(1, b"\x00")
        with self.assertRaises(tool.FormatError):
            tool.load(with_empty)
        self.reject_snapshot_bytes(with_empty, module)

        postmortem = tool.Snapshot()
        postmortem.flags = 1
        raw = tool.pack(postmortem)
        self.assertEqual([k for k, _ in sections(raw)], [0])
        self.assertEqual(tool.pack(tool.load(raw)), raw)

    def test_store_sections_are_required(self):
        for kind in (1, 2, 3, 4):
            with self.subTest(kind=kind):
                self.reject(
                    HEADER
                    + b"".join(section(k, b) for k, b in self.parts if k != kind),
                    python=False,
                )

    def test_vector_locals_travel_whole(self):
        frame = tool.load(self.raw).continuations[0].activations[-1]
        vectors = [v for t, v in frame.values if t == tool.VALTYPE_V128]
        self.assertEqual(vectors, [bytes(16)])

    def test_integer_encodings(self):
        meta = self.parts[0][1]
        # an overlong u64: the timestamp, straight after the flags
        self.reject(self.replace(0, meta[:1] + b"\xff" * 9 + b"\x7f" + meta[2:]))
        for encoded in (b"\x80\x80\x80\x80\x08", b"\x80" * 5 + b"\x00"):
            with self.subTest(encoded=encoded):
                self.reject(self.replace(3, b"\x02\x00\x7f" + encoded))
        for encoded, expected in (
            (b"\x80\x80\x80\x80\x78", -(1 << 31)),
            (b"\xff\xff\xff\xff\x07", (1 << 31) - 1),
            (b"\x80\x00", 0),
        ):
            self.assertEqual(tool._Cursor(encoded).leb_i32(), expected)

    def test_memory_and_table_bounds(self):
        # table count, index, first index, funcref, size 1, null element; the
        # declared maximum is 0
        self.reject(self.replace(2, b"\x01\x00\x00\x70\x01\x00"), python=False)
        # memory count, index, first index, 64 KiB pages, then the page count.
        # A fill whose offset + length wraps u32 must be rejected on every host.
        chunk = b"\x01\x00\x00\x10\x01\x02" + leb(0xFFFFFFF0) + leb(32) + b"\x00"
        self.reject(self.replace(1, chunk), python=False)
        self.reject(
            self.replace(1, b"\x01\x00\x00\x10\x02\x00"), python=False
        )  # page maximum 1
        # a page size other than the module's
        self.reject(self.replace(1, b"\x01\x00\x00\x00\x01\x00"), python=False)
        # shared with a memory, or a table, the module does not share it with
        self.reject(self.replace(1, b"\x01\x00\x01"))
        self.reject(self.replace(2, b"\x01\x00\x01"))

    def test_boolean_fields(self):
        segments = dict(self.parts)[4]
        self.assertEqual(segments[0], 1)
        self.reject(self.replace(4, segments[:1] + b"\x02" + segments[2:]))
        conts = bytearray(dict(self.parts)[6])
        self.assertEqual(conts[:4], b"\x01\x00\x01\x01")
        conts[3] = 2
        self.reject(self.replace(6, conts))

    def test_postmortem_grammar(self):
        snapshot = tool.Snapshot()
        snapshot.flags = 1
        snapshot.continuations = [
            tool.Continuation(
                id=0,
                state=tool.CONT_FINISHED,
                is_root=False,
                type_index=0,
                entry_func_index=0,
                resume_throw=None,
            )
        ]
        raw = tool.pack(snapshot)
        self.assertEqual(dict(sections(raw))[6], b"\x01\x00\x02\x00\x00\x00\x00")
        self.assertEqual(tool.pack(tool.load(raw)), raw)
        self.assertEqual(tool.verify(tool.load(raw)), [])

    def test_exception_tag_is_only_in_meta(self):
        snapshot = tool.Snapshot()
        snapshot.flags = 1
        snapshot.exceptions = [
            tool.Exception_(tag_index=7, args=[(tool.VALTYPE_I64, 42)])
        ]
        raw = tool.pack(snapshot)
        self.assertEqual(dict(sections(raw))[5], b"\x01\x00\x01\x7e\x2a")
        self.assertEqual(tool.load(raw).exceptions[0].tag_index, 7)
        self.assertEqual(tool.pack(tool.load(raw)), raw)

    def assemble(self, source):
        """The module a .wat, or the first module a .wast script, assembles to."""
        is_script = source.suffix == ".wast"
        output = self.directory / (source.stem + (".json" if is_script else ".wasm"))
        result = self.command(
            "--stack-size",
            "1048576",
            ROOT
            / "test/wasi/wabt"
            / ("wast2json.wasm" if is_script else "wat2wasm.wasm"),
            "--enable-all",
            source,
            "-o",
            output,
            executable=HOST,
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        if is_script:
            return (
                output.parent
                / json.loads(output.read_text())["commands"][0]["filename"]
            )
        return output

    def samples(self, name):
        if name in self.fixture_samples:
            return self.fixture_samples[name]
        module = self.assemble(ROOT / "test/snapshot" / name)
        samples = {}
        # Each budget starts from a fresh instance. Every observed stage must
        # resume without trapping, including a second save/load in Python.
        for gas in range(1, 250):
            path = self.directory / "sample.dmp"
            path.unlink(missing_ok=True)
            result = self.command("--gas-limit", gas, "--snapshot", path, module)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            if not path.exists():
                break
            raw = path.read_bytes()
            snap = tool.load(raw)
            if snap.is_postmortem:
                break
            self.assertEqual(tool.pack(snap), raw)
            result = self.command("--resume", path, module)
            self.assertEqual(
                result.returncode, 0, f"gas={gas}: {result.stderr.decode()}"
            )
            stage = snap.globals[0][1]
            samples.setdefault(stage, snap)
        else:
            self.fail("fixture did not complete within the gas budget")
        self.fixture_samples[name] = (module, samples)
        return module, samples

    def reject_snapshot(self, snap, module):
        return self.reject_snapshot_bytes(tool.pack(snap), module)

    def reject_snapshot_bytes(self, raw, module):
        result = self.command("--resume", self.write("bad-ref.dmp", raw), module)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"failed loading snapshot", result.stderr)
        return result

    def test_bound_continuations(self):
        module, samples = self.samples("bind.wast")
        self.assertTrue(set(range(1, 7)) <= samples.keys())
        for stage in range(1, 7):
            snap = samples[stage]
            count = (stage - 1) % 3 + 1
            cont = snap.continuations[snap.globals[count][1]]
            self.assertEqual(cont.bound_args_count, count)
            self.assertEqual(
                cont.state, tool.CONT_ALLOCATED if stage <= 3 else tool.CONT_SUSPENDED
            )
            if stage <= 3:
                self.assertEqual(
                    [t for t, _ in cont.args],
                    [tool.VALTYPE_I32, tool.VALTYPE_F64, tool.VALTYPE_FUNCREF][:count],
                )
            directory = self.directory / f"unpacked-{stage}"
            tool.unpack(snap, directory)
            rebuilt = tool.pack_directory(directory)
            raw = tool.pack(rebuilt)
            self.assertEqual(raw, tool.pack(snap))
            result = self.command("--resume", self.write("repacked.dmp", raw), module)
            self.assertEqual(result.returncode, 0, result.stderr.decode())

            bad = copy.deepcopy(snap)
            bad.continuations[cont.id].bound_args_count = count + 1
            if stage <= 3:
                bad.continuations[cont.id].args.append((tool.VALTYPE_I32, 0))
            self.reject_snapshot(bad, module)

        # The global references its continuation before that object's type is
        # available. Same base kind and valid ID, but a different cont signature.
        bad = copy.deepcopy(samples[2])
        bad.globals[2] = (tool.VALTYPE_CONTREF, bad.globals[1][1])
        self.reject_snapshot(bad, module)
        bad = copy.deepcopy(samples[1])
        bad.continuations[bad.globals[1][1]].type_index = 0  # function, not cont
        self.reject_snapshot(bad, module)

        # A bound non-null reference lives in the innermost captured frame,
        # while the binding count belongs to its outer wrapper.
        bad = copy.deepcopy(samples[6])
        outer = bad.continuations[bad.globals[3][1]]
        inner = bad.continuations[outer.activations[-1].cont_id]
        self.assertEqual(inner.activations[-1].values[-1][0], tool.VALTYPE_FUNCREF)
        inner.activations[-1].values[-1] = (tool.VALTYPE_FUNCREF, tool.NULL_REF)
        self.reject_snapshot(bad, module)

        bad = copy.deepcopy(samples[6])
        outer = bad.continuations[bad.globals[3][1]]
        outer.activations[-1].cont_id = outer.id  # cyclic resume chain
        self.assertTrue(any("cyclic resume chain" in p for p in tool.verify(bad)))
        self.reject_snapshot(bad, module)

        bad = copy.deepcopy(samples[6])
        outer = bad.continuations[bad.globals[3][1]]
        outer.activations[-1].cont_id = 0  # target root
        self.assertTrue(any("cannot target the root" in p for p in tool.verify(bad)))
        self.reject_snapshot(bad, module)

        bad = copy.deepcopy(samples[6])
        bad.continuations[0].bound_args_count = 1
        self.assertTrue(
            any(
                "root continuation cannot have bound arguments" in p
                for p in tool.verify(bad)
            )
        )

        bad = copy.deepcopy(samples[6])
        finished = copy.deepcopy(bad.continuations[0])
        finished.id = len(bad.continuations)
        finished.is_root = 0
        finished.state = tool.CONT_FINISHED
        finished.bound_args_count = 1
        bad.continuations.append(finished)
        self.assertTrue(
            any(
                "finished continuation has bound arguments" in p
                for p in tool.verify(bad)
            )
        )

        # a second continuation resuming the one the first already runs
        bad = copy.deepcopy(samples[6])
        outer = bad.continuations[bad.globals[3][1]]
        twin = copy.deepcopy(outer)
        twin.id = len(bad.continuations)
        bad.continuations.append(twin)
        bad.total_continuation_stacks += 1
        self.assertTrue(any("more than one frame" in p for p in tool.verify(bad)))
        result = self.reject_snapshot(bad, module)
        self.assertIn(b"resumed by two frames", result.stderr)

        # A bound reference must have its original prefix parameter's type.
        bad = copy.deepcopy(samples[3])
        bad.continuations[bad.globals[3][1]].args[-1] = (tool.VALTYPE_FUNCREF, 1)
        self.reject_snapshot(bad, module)

    def test_reference_types(self):
        module, samples = self.samples("references.wat")
        snap = samples[1]
        for index, value in [(1, 1), (2, tool.NULL_REF)]:
            bad = copy.deepcopy(snap)
            bad.globals[index] = (tool.VALTYPE_FUNCREF, value)
            self.reject_snapshot(bad, module)
        bad = copy.deepcopy(snap)
        bad.tables[0][1][0] = 1  # valid function index, wrong signature
        self.reject_snapshot(bad, module)
        bad = copy.deepcopy(snap)
        frame = bad.continuations[0].activations[0]
        self.assertEqual(frame.values[0][0], tool.VALTYPE_FUNCREF)
        frame.values[0] = (tool.VALTYPE_FUNCREF, 1)
        self.reject_snapshot(bad, module)
        bad = copy.deepcopy(snap)
        bad.continuations[0].activations[0].values[1] = (
            tool.VALTYPE_FUNCREF,
            tool.NULL_REF,
        )
        self.reject_snapshot(bad, module)
        bad = copy.deepcopy(samples[2])
        frame = bad.continuations[0].activations[0]
        self.assertEqual(frame.values[-1][0], tool.VALTYPE_FUNCREF)
        frame.values[-1] = (tool.VALTYPE_FUNCREF, tool.NULL_REF)
        self.reject_snapshot(bad, module)

        # Wire tag says funcref but its payload names another reference family.
        raw = tool.pack(snap)
        parts = sections(raw)
        for kind in (2, 3, 4, 5):
            bodies = dict(parts)
            globals_body = bytearray(bodies[3])
            cursor = tool._Cursor(globals_body)
            cursor.leb_u32()
            cursor.leb_u32()
            cursor.read_value()
            cursor.leb_u32()
            self.assertEqual(cursor.u8(), tool.VALTYPE_FUNCREF)
            globals_body[cursor.pos] = kind
            malformed = HEADER + b"".join(
                section(k, globals_body if k == 3 else b) for k, b in parts
            )
            with self.assertRaises(tool.FormatError):
                tool.load(malformed)
            result = self.command(
                "--resume", self.write("wrong-kind.dmp", malformed), module
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"failed loading snapshot", result.stderr)

    def test_mismatched_resource_totals(self):
        # 1. Raising totals (+1)
        module, samples = self.samples("references.wat")
        snap = samples[1]

        # Mismatched memory bytes
        bad = copy.deepcopy(snap)
        bad.total_memory_bytes += 1
        self.assertTrue(
            any("memory bytes count does not match Meta" in p for p in tool.verify(bad))
        )
        self.reject_snapshot_bytes(tool.pack(bad, recompute_totals=False), module)

        # Mismatched table elements
        bad = copy.deepcopy(snap)
        bad.total_table_elements += 1
        self.assertTrue(
            any(
                "table element count does not match Meta" in p for p in tool.verify(bad)
            )
        )
        self.reject_snapshot_bytes(tool.pack(bad, recompute_totals=False), module)

        # Mismatched continuation stacks
        bad = copy.deepcopy(snap)
        bad.total_continuation_stacks += 1
        self.assertTrue(
            any(
                "active continuation stack count does not match Meta" in p
                for p in tool.verify(bad)
            )
        )
        self.reject_snapshot_bytes(tool.pack(bad, recompute_totals=False), module)

        # 2. Lowering totals under limits to test understated totals
        grow_wat = (
            "(module\n"
            "  (memory 1)\n"
            "  (table 2 funcref)\n"
            '  (func (export "_start")\n'
            "    i32.const 1 memory.grow drop\n"
            "    ref.null func i32.const 2 table.grow drop\n"
            "    loop $l br $l end))\n"
        )
        grow_wasm = self.write("grow-resources.wasm", b"")
        wat_path = self.write("grow-resources.wat", grow_wat.encode())
        res = self.command(
            "--stack-size",
            "1048576",
            ROOT / "test/wasi/wabt/wat2wasm.wasm",
            "--enable-all",
            wat_path,
            "-o",
            grow_wasm,
            executable=HOST,
        )
        self.assertEqual(res.returncode, 0)
        grow_dmp = self.directory / "grow-resources.dmp"
        res = self.command("--gas-limit", "20", "--snapshot", grow_dmp, grow_wasm)
        self.assertTrue(grow_dmp.exists())
        grow_snap = tool.load(grow_dmp)
        self.assertEqual(grow_snap.total_memory_bytes, 131072)
        self.assertEqual(grow_snap.total_table_elements, 4)

        # Understate memory: claim 65536 bytes (the initial allocation) instead of 131072
        bad_mem = copy.deepcopy(grow_snap)
        bad_mem.total_memory_bytes = 65536
        self.assertTrue(
            any(
                "memory bytes count does not match Meta" in p
                for p in tool.verify(bad_mem)
            )
        )
        raw_mem = tool.pack(bad_mem, recompute_totals=False)
        result = self.command(
            "--max-memory",
            "65536",
            "--resume",
            self.write("understated-mem.dmp", raw_mem),
            grow_wasm,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"failed loading snapshot", result.stderr)

        # Understate table: claim 2 elements (the initial allocation) instead of 4
        bad_tbl = copy.deepcopy(grow_snap)
        bad_tbl.total_table_elements = 2
        self.assertTrue(
            any(
                "table element count does not match Meta" in p
                for p in tool.verify(bad_tbl)
            )
        )
        raw_tbl = tool.pack(bad_tbl, recompute_totals=False)
        result = self.command(
            "--max-table-elements",
            "2",
            "--resume",
            self.write("understated-tbl.dmp", raw_tbl),
            grow_wasm,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"failed loading snapshot", result.stderr)

        # Understate continuation stacks:
        bind_module, bind_samples = self.samples("bind.wast")
        cont_snap = bind_samples[1]
        self.assertGreater(cont_snap.total_continuation_stacks, 0)
        bad_cont = copy.deepcopy(cont_snap)
        bad_cont.total_continuation_stacks = 0
        self.assertTrue(
            any(
                "active continuation stack count does not match Meta" in p
                for p in tool.verify(bad_cont)
            )
        )
        raw_cont = tool.pack(bad_cont, recompute_totals=False)
        result = self.command(
            "--max-continuations",
            "0",
            "--resume",
            self.write("understated-cont.dmp", raw_cont),
            bind_module,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"failed loading snapshot", result.stderr)

    def step_to(self, name, module, until, what):
        """The first snapshot `run` pauses at that `until` accepts, stepping from
        one pause point to the next: every run after the first resumes the last
        snapshot with a pause requested again."""
        path = self.directory / f"{name}-0.dmp"
        path.unlink(missing_ok=True)
        result = self.command(
            "--interrupt", "--snapshot", path, "--func", "run", module
        )
        for step in range(1, 100):
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertTrue(path.exists(), f"finished without stopping at {what}")
            snap = tool.load(str(path), module=str(module))
            if until(snap):
                return path, snap
            following = self.directory / f"{name}-{step}.dmp"
            following.unlink(missing_ok=True)
            result = self.command(
                "--interrupt", "--resume", path, "--snapshot", following, module
            )
            self.assertFalse(
                following.exists() and following.read_bytes() == path.read_bytes(),
                f"stopped where it started, short of {what}",
            )
            path = following
        self.fail(f"never stopped at {what}")

    def pause_example(self, source, module, expected):
        """A snapshot of the example paused where each continuation expected
        names has its innermost frame at the function and site expected of it."""

        def arrived(snap):
            for selector, frames in expected.items():
                try:
                    a = example_continuation(snap, selector).activations[-1]
                except (IndexError, KeyError, TypeError, AttributeError):
                    return False
                if example_frame(snap, a).split()[:2] != frames[-1].split()[:2]:
                    return False
            return True

        return self.step_to(source.stem, module, arrived, expected)

    def body_offsets(self, module, func_index, name):
        """Where each `name` instruction of a defined function starts, counted
        the way a snapshot counts a wasm_offset, out of the bundled disassembler."""
        result = self.command(
            "--stack-size",
            "1048576",
            ROOT / "test/wasi/wabt/wasm-objdump.wasm",
            "-d",
            module,
            executable=HOST,
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        body = tool.ModuleInfo.read(str(module)).bodies[func_index][0]
        offsets, current = [], None
        for line in result.stdout.decode().splitlines():
            if " func[" in line and line.rstrip().endswith(":"):
                current = int(line.split("func[")[1].split("]")[0])
            elif current == func_index and "|" in line:
                where, _, text = line.partition("|")
                if text.split() and text.split()[0] == name:
                    offsets.append(int(where.split(":")[0], 16) - body)
        return offsets

    def test_example_frames(self):
        sources = sorted(EXAMPLES.glob("*.wa[st]*"))
        self.assertTrue(sources)
        for source in sources:
            with self.subTest(source.name):
                pause, expected = example_expectations(source)
                self.assertEqual(pause, "interrupt")
                module = self.assemble(source)
                path, snap = self.pause_example(source, module, expected)
                self.assertEqual(tool.verify(snap), [])
                for selector, frames in expected.items():
                    cont = example_continuation(snap, selector)
                    self.assertEqual(
                        [example_frame(snap, a) for a in cont.activations],
                        frames,
                        f"{source.name}: {selector}",
                    )
                result = self.command("--resume", path, module)
                self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_frame_function_returns_what_is_expected(self):
        # A tail call means the function standing in a frame need not be the one
        # the call named, but it has to return what that call expects.
        source = EXAMPLES / "tail-calls.wat"
        module = self.assemble(source)
        _, snap = self.pause_example(source, module, {"root": ["0 back-edge"]})
        bad = copy.deepcopy(snap)
        bad.continuations[0].activations[1].func_index = 3  # `run` returns nothing
        result = self.reject_snapshot(bad, module)
        self.assertIn(b"does not return what its caller expects", result.stderr)

    def test_back_edge_names_its_loop(self):
        source = ROOT / "test/snapshot/back-edge-target.wat"
        module = self.assemble(source)
        pre, x, outer, inner = self.body_offsets(module, 0, "loop")
        (done,) = self.body_offsets(module, 0, "block")

        def back_edge_to(loop):
            def arrived(snap):
                a = snap.continuations[0].activations[-1]
                return a.site == tool.SITE_OP and a.target_loop == loop

            return arrived

        # a pause at the br_table's back edge to either loop resumes to 604
        to_inner, snap = self.step_to("to-inner", module, back_edge_to(inner), "$in")
        result = self.command("--resume", to_inner, module)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        to_outer, _ = self.step_to("to-outer", module, back_edge_to(outer), "$o")
        result = self.command("--resume", to_outer, module)
        self.assertEqual(result.returncode, 0, result.stderr.decode())

        # The first pause, sent round the other loop: the frames have the same
        # shape, and it goes where the snapshot says, for 605.
        edited = copy.deepcopy(snap)
        edited.continuations[0].activations[-1].target_loop = outer
        result = self.command(
            "--resume", self.write("sent-round.dmp", tool.pack(edited)), module
        )
        self.assertIn(b"[trap] unreachable", result.stderr)

        for target, message in (
            (done, b"does not go to a loop of its function"),
            (pre, b"goes to a loop that is not around it"),
            (x, b"is not at a safepoint of this build's"),
        ):
            with self.subTest(hex(target)):
                bad = copy.deepcopy(snap)
                bad.continuations[0].activations[-1].target_loop = target
                result = self.reject_snapshot(bad, module)
                self.assertIn(message, result.stderr)

    def test_pause_points_follow_the_module(self):
        # Every pause point Wasm3 compiles is one the proposal's list puts in the
        # module, and every one it puts there is compiled - worked out from the
        # instructions alone, by the disassembler, which cannot read the
        # stack-switching examples
        if not M3_TEST.exists():
            self.skipTest(f"no {M3_TEST}")
        modules = [
            self.assemble(source)
            for source in sorted(EXAMPLES.glob("*.wat"))
            + [ROOT / "test/snapshot/back-edge-target.wat"]
        ] + [
            ROOT / "test/wasi/mandelbrot/mandel.wasm",
            ROOT / "test/wasi/mandelbrot/mandel_dd.wasm",
            ROOT / "test/wasi/smallpt/smallpt-ex.wasm",
            ROOT / "test/wasi/smallpt/smallpt-ex-mv.wasm",
            ROOT / "test/wasi/coremark/coremark.wasm",
        ]
        for module in modules:
            with self.subTest(module.name):
                expected = sorted(
                    pause_points.describe(p)
                    for p in pause_points.pause_points(
                        pause_points.disassemble(str(module), shlex.join(HOST))
                    )
                )
                result = subprocess.run(
                    [str(M3_TEST), "--pause-points", str(module)],
                    capture_output=True,
                    timeout=60,
                )
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                compiled = sorted(set(result.stdout.decode().splitlines()))
                self.assertEqual(compiled, expected)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]] + unittest_args)
