# Portable W3S snapshots

Snapshots save and resume a suspended runtime. This plan covers what it takes
for one to survive the trip to a different machine: a different architecture, a
different operating system, or a different build of Wasm3.

The short of it: the format writes down interpreter state, and a portable
snapshot has to write down Wasm state instead.

## Where it stands

`--snapshot run.w3s` writes the state of a suspended runtime and `--resume`
brings it back. The feature is built on the stack-switching machinery:
suspension reifies the native frames into an `M3Frame` list, and
`m3_SaveSnapshot` writes those frames alongside linear memory, globals, tables
and the value stack - for the paused call and for every continuation it can
reach.

While a runtime is suspendable, the compiler records a snapshot map for each
function (`M3SnapshotMap`, in `m3_function.h`):

- **Code runs** turn a pc into a count of the function's metacode words and
  back, bridges not counted, so the same instruction is named in any process
  running the same build.
- **Safepoints** sit at every place a frame can be left standing: loop back
  edges, gas charges, calls, suspends, switches and resumes. Each one lists the
  slots, and the register, that hold references there.

With those, the format never writes an address. A `funcref` is a function
index; a `contref` or `exnref` is the id of a continuation or exception the
snapshot carries whole. The header pins everything else the file is expressed
in: byte order, pointer and slot widths, a build fingerprint, whether gas
metering was on, and a hash of the module. A mismatch is refused.

[`extra/w3s-tool.py`](../extra/w3s-tool.py) is the reference for the layout.

## What still ties a snapshot to one build

**The program counter names a place in metacode.** A pc is a count of metacode
words. It changes with the Wasm3 revision, with the config, and with gas
metering, because instrumentation moves the emission.

**Only references are typed.** The value stack goes out as the build's own slots,
in host byte order. The safepoints say which slots hold references, and nothing
says what the rest hold - so a different slot width or byte order cannot be
translated.

**Registers and frames are the build's own.** `r0` and `fp0` are saved raw, and
the frame list mirrors the interpreter's native frames: a loop, a try region and
an entry frame are there because this interpreter keeps a native frame for them.

**An `externref` is refused.** It belongs to the embedder, and nothing lets the
embedder name one.

## The idea

A portable snapshot never writes down a layout. The program counter becomes a
function index plus a byte offset into that function's Wasm body. A slot becomes
a typed value. On arrival the destination recompiles, maps the Wasm offset back
to its own metacode, and places the values into whatever slots *its* compiler
chose.

That last step is what buys the independence. The two builds never have to agree
on a layout, only on the Wasm - so the same snapshot survives a different
architecture, a different slot width, a different Wasm3 revision, and gas
metering being toggled.

| Piece | Where |
|---|---|
| pc to Wasm byte offset | `RecordSafePoint` records `wasmOffset` directly in `M3SafePoint` (zero interpreter cost, avoids heavy `d_m3RecordBacktraces` tables) |
| Native frames as data | `M3Frame` and `ReplayFrames`, from stack switching |
| Live values at a safepoint | `RecordSafePoint`, walking `typeStack` and `wasmStack` to capture canonical locals and live operand stack |
| References by name | `m3_snapshot.c` |

## Plan

### Phase 1 - type every live value

Extend safepoint collection from references to all live values:
- Function locals ($0 \dots M-1$) in declaration order (types fixed by function signature and local decls).
- Live operand stack entries ($0 \dots K-1$) in evaluation order (types tracked in compiler's `typeStack`).
- Register destination (`_r0` or `_fp0`) if top of stack is cached.

The liveness rules already hold - a block's landing pads are not values, and a
back edge writes its loop's parameters - so this is a matter of recording more
of what `RecordSafePoint` already walks at compile time.

**Performance impact:**
- **Interpreter runtime:** 0.0% overhead. No dispatch loops, opcodes, or hot paths are modified.
- **Compiler:** Negligible. A simple subtraction (`o->lastOpcodeStart - o->function->wasm`) and compact type recording only at safepoints (avoiding the heavy opcode-by-opcode tables of `d_m3RecordBacktraces`).

### Phase 2 - make W3S host-agnostic (Format v1)

Since W3S is unreleased, the format remains Version 1 (`W3S\x01`) and is made
host-agnostic directly:

1. **Canonical Little-Endian:** All multi-byte integers, floats, and memory in
   transit are encoded in canonical Little-Endian (matching the WebAssembly
   specification). The `byte_order` marker is removed from the header.
2. **Remove host layout and configuration metadata:** `pointer_size`, `slot_size`,
   and `gas_metered` are removed from the header entirely. In the host-tied
   format, `gas_metered` existed solely to prevent metacode PC drift caused by
   gas instrumentation opcodes; in portable W3S, Wasm bytecode offsets are
   decoupled from gas instrumentation. The snapshot carries execution state,
   while gas limits remain host runtime policy.
3. **Snapshot timestamp:** Include `timestamp_ms` (`u64`, 8 bytes) in the header
   — a millisecond-level Unix timestamp (UTC milliseconds since epoch)
   recording when the snapshot was taken.
4. **Canonical values:** Values are serialized as canonical Wasm state (locals in
   declaration order, then operand stack in evaluation order) normalized to
   64-bit LE, rather than as raw host-dependent interpreter slots.
5. **Program counters:** Recorded as `(function_index, wasm_offset)` relative
   to the function body.
6. **Header validation:** `BuildFingerprint` checks semantic feature compatibility
   (e.g. float support) rather than internal metacode word emission.
7. **Host references:** Embedder hooks for naming and re-binding `externref`.

#### Updated 40-byte header layout

| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `magic` | `u8[4]` | ASCII `"W3S\x01"` |
| 4 | `flags` | `u32` | `0x1` = postmortem dump; `0x0` = resumable |
| 8 | `timestamp_ms` | `u64` | Millisecond-level Unix timestamp (UTC) |
| 16 | `wasm3_hash` | `u64` | FNV-1a hash of Wasm3 version string and feature configuration |
| 24 | `module_hash` | `u64` | FNV-1a hash of the module's raw WebAssembly bytecode |
| 32 | `num_continuations` | `u32` | Total number of continuations stored (index 0 is root) |
| 36 | `num_exceptions` | `u32` | Total number of exceptions stored |

### Phase 3 - restore by re-materialisation

Restore becomes a clean 5-step pipeline:
1. Re-instantiate the module and compile the functions the snapshot references.
2. For each suspended frame, binary search the destination's sorted
   `safePoints` array by `wasm_offset` to find the target `pc` and `M3SafePoint`.
3. Inspect the target safepoint's slot/register map (`wasmStack`, `_r0`, `_fp0`).
4. Scatter canonical Wasm values (locals and operands) from the snapshot into
   the destination's physical slots and registers, performing 64-to-32 slot
   adaptation or byte-swapping if the target host differs.
5. Reconstruct the `M3Frame` list and hand off to `ReplayFrames` to resume.

### Phase 4 - draw the line at the host

Open descriptors, the working directory, preopens, clocks and entropy live
outside the Wasm state. Ship the honest subset first - nothing open beyond
preopens - and design the hook that lets an embedder serialize its own context.
Reopening files by path and offset is reasonable for regular files and wrong for
sockets and pipes, so it is the embedder's call and not the engine's.

## Performance profile

- **Interpreter execution:** Exactly 0.0% runtime overhead. Execution speed is
  identical before, during, and after migration.
- **Compilation overhead:** Negligible. 4 bytes (`wasmOffset`) added per
  `M3SafePoint` and lightweight slot typing during existing `RecordSafePoint`
  passes. No full-module backtrace tables required.
- **Snapshot save/restore:** Fast contiguous serialization and $O(\log N)$ binary
  search lookup during re-materialisation. Checkpointing is non-intrusive.

## How to know it works

| Test | What it covers |
|---|---|
| Round trip | `snapshot.round_trip_at_every_gas_stop`: a program stopped at every gas charge, restored into a fresh runtime each time, run to the end against the reference answer. |
| **Cross-config** | Capture under `-Dd_m3Use32BitSlots=1`, restore under the 64-bit-slot build; then with and without gas metering. **This is the test that proves the snapshot carries Wasm state and not interpreter state.** |
| Cross-endian | A big-endian target under qemu. `build-cross.py` has the scaffolding. |
| Cross-OS | Windows and WSL on the same architecture, same `.wasm`, snapshot moved between them. |
| Soak | Migrate every N instructions through the WASI test apps and check the output never changes. |
| Format oracle | [`extra/w3s-tool.py`](../extra/w3s-tool.py) parses a snapshot independently of the C code. If it cannot read what Wasm3 wrote, one of the two is wrong. |

## Tooling

[`extra/w3s-tool.py`](../extra/w3s-tool.py) reads the format independently
of the engine: a summary, an unpack to JSON plus one binary per blob, a repack
that is byte-identical, a structural check, and a diff between two checkpoints
of the same program. It imports as a module, so external tooling can read and
rewrite snapshots without going through Wasm3.

```sh
# what is in this snapshot, with names from the module
$ extra/w3s-tool.py --wasm count.wasm info count.w3s

# open it up; edit snapshot.json or memory0.bin; close it again
$ extra/w3s-tool.py unpack count.w3s -o count.d
$ extra/w3s-tool.py pack   count.d   -o patched.w3s

# what moved between two checkpoints
$ extra/w3s-tool.py diff before.w3s after.w3s
```

## Scope

Migration becomes possible *at chosen points*, not at any instant. A program
blocked inside a host call never reaches one, which is the same boundary
`RunCodeChecked` already enforces by clearing `activeContinuation`: the native
frames on the far side of an import are not ours to describe.

Phases 1 to 3 are ordinary engineering with a visible finish line. Phase 4 does
not have one, and should be scoped deliberately rather than allowed to grow.
