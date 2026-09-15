# Portable W3S snapshots

Snapshots already save and resume a suspended runtime. This plan covers what it
takes for one to survive the trip to a different machine: a different
architecture, a different operating system, or a different build of Wasm3.

The short of it: version 1 writes down interpreter state, and a portable
snapshot has to write down Wasm state instead.

## Where it stands

`--suspendable --snapshot run.w3s` writes the state of a suspended root
continuation and `--resume` brings it back. The feature is built on the
stack-switching machinery: suspension reifies the native frames into an
`M3Frame` list, and `m3_SaveSnapshot` writes that list alongside linear memory,
globals, tables and the value stack.

Everything in the format is expressed in one build's own terms. Two fields
carry that assumption: the program counter is an offset into compiled metacode,
and the value stack is a block of raw untyped slots. [Cookbook.md](../docs/Cookbook.md)
already warns that toggling gas metering invalidates a snapshot, which is the
same problem seen from inside a single build.

## What version 1 records

Every field is written with its exact width and no padding, so the stream is
packed - but each one goes out in host byte order. The table below is a real
384-byte snapshot: a counting loop suspended by gas exhaustion, x86-64 Linux,
4-byte slots.

| Offset | Bytes | Field | Value | Travels? |
|---|---|---|---|---|
| `0x000` | 4 | magic | `'W3SS'` | yes |
| `0x004` | 4 | version | 1 | yes |
| `0x008` | 4 | flags | 0 | unused |
| `0x00c` | 4 | numMemories | 1 | yes |
| `0x010` | 8 | numPages | 1 | yes |
| `0x018` | 8 | maxPages | 65536 | yes |
| `0x020` | 1 | chunk END | memory all zero | yes |
| `0x021` | 4 | numGlobals | 1 | yes |
| `0x025` | 1 | global[0].type | 1 (i32) | yes |
| `0x026` | 8 | global[0].value | 26595 | **host pointer when the value is a reference** |
| `0x02e` | 4 | numTables | 0 | yes |
| `0x032` | 4 | spSlot | 0 | **slot width** |
| `0x036` | 4 | numSlotsToSave | 64 (`sp + 64`) | **heuristic** |
| `0x03a` | 256 | value stack | 64 x 4 bytes, untyped | **this build only** |
| `0x13a` | 4 | entryFuncIndex | 1 (`run`) | yes |
| `0x13e` | 4 | currentFuncIndex | 1 (`run`) | yes |
| `0x142` | 4 | pcOffset | 5 metacode words | **this build only** |
| `0x146` | 8 | r0 | 0 | **untyped** |
| `0x14e` | 8 | fp0 | 0.0 | yes |
| `0x156` | 4 | numFrames | 1 | yes |
| `0x15a` | 1 | frame[0].kind | 1 (loop) | **enum depends on build** |
| `0x15b` | 4 | frame[0].spSlot | 0 | **slot width** |
| `0x15f` | 4 | frame[0].funcIndex | 1 (`run`) | yes |
| `0x163` | 4 | frame[0].pcOffset | 5 metacode words | **this build only** |
| `0x167` | 8 | frame[0].r0 | 0 | **untyped** |
| `0x16f` | 8 | frame[0].fp0 | 0.0 | yes |
| `0x177` | 4 | frame[0].calleeIndex | 0 | yes |
| `0x17b` | 4 | frame[0].numClauses | 0 | yes |
| `0x17f` | 1 | frame[0].handlersLive | 0 | yes |

Frames are written outermost first, the reverse of the innermost-first order
`M3Continuation.frames` holds them in.

The magic is the one field that self-describes: `0x53533357` written natively
comes out as `57 33 53 53` on a little-endian host and reversed on a big-endian
one, so the file does say which way round it is. Nothing else does.

## What has to change

### Wrong today

These three are defects in the current format, independent of portability.

**The value stack is saved by guess.** `numSlotsToSave = spSlot + 64`, capped at
the runtime's stack size. A function whose frame needs more than 64 slots comes
back with its locals and working space partly filled with whatever the
destination stack happened to hold. The bound should be the innermost
function's `maxStackSlots`, which the compiler already knows.

**Nothing identifies the module or the build.** `--resume` takes a `.wasm` on
faith. Hand it a different module and the function indices and metacode offsets
land somewhere arbitrary. A hash of the module bytes plus a build fingerprint -
Wasm3 revision, slot width, and whether gas metering was on - turns silent
corruption into a refusal.

**The memory chunk stream is conditional on two different things.** The writer
emits it when `memory && memory->mallocated`; the reader consumes it when
`memory`. Neither condition is in the file, so a memory that exists without
allocation desyncs the stream and every field after it. Latent rather than
live, but the two should agree.

### Blocks the move

**Slots are raw and untyped.** The stack is copied out whole. Its width follows
`d_m3Use32BitSlots`, its contents are in host byte order, and any slot holding a
reference holds a pointer into this process. Nothing in the file says which
slots are which.

**The program counter names a place in metacode.** `pcOffset` counts words into
an array of `op_*` addresses. It changes with the Wasm3 revision, with the
config, and with gas metering, because instrumentation moves the emission.

**References are host pointers.** Tables are written as function indices, which
is right, but only for `funcref`. A global or a slot holding an `externref`,
`exnref` or `contref` is written as a raw `u64` of whatever the pointer was.

**A captured resume is refused outright.** `m3_SaveSnapshot` rejects any
`frame_resume`: the format has no room for the second continuation it points at.
Correct and honest, but nested prompts are implemented now, so the format is the
only thing holding this back.

## The idea

A portable snapshot never writes down an address or a layout. The program
counter becomes a function index plus a byte offset into that function's Wasm
body. A slot becomes a typed value. A reference becomes an index into the thing
it refers to. On arrival the destination recompiles, maps the Wasm offset back
to its own metacode, and places the values into whatever slots *its* compiler
chose.

That last step is what buys the independence. The two builds never have to agree
on a layout, only on the Wasm - so the same snapshot survives a different
architecture, a different slot width, a different Wasm3 revision, and gas
metering being toggled.

Two of the three pieces exist:

| Piece | Where |
|---|---|
| pc to Wasm byte offset | `EmitMappingEntry` records one per emitted operation, `MapPCToOffset` reads it back - today only under `d_m3RecordBacktraces` |
| Native frames as data | `M3Frame` and `ReplayFrames`, from stack switching |
| **Type of each live slot** | **missing** - the compiler holds it in `typeStack` and `wasmStack` and discards it when the function finishes |

## Plan

### Phase 0 - fix version 1, and make it self-describing

Bound the saved stack by `maxStackSlots` instead of the `+64` guess. Put the
slot width, the endianness, a module hash and a build fingerprint in the header,
and refuse a mismatch rather than resuming into nonsense. Make the two
memory-chunk conditions agree.

None of this needs the portable format. It makes what exists trustworthy and
gives the later work a header to grow into.

### Phase 1 - spike the slot map

Before anything is designed around it: can the compiler emit, for a chosen
point, the type of every live slot? Take `typeStack` and `wasmStack` at a loop
back edge and write the table out. If the stack state turns out not to be
reconstructible where capture needs to happen, the shape of the plan changes -
so find out cheaply. This is the long pole.

### Phase 2 - decide the safepoints

Capture cannot happen at an arbitrary operation, only where the live state is
describable: function entry, call sites, and loop back edges. `m3_Yield` already
sits at the first two and suspension already uses the third.

Emit slot maps at exactly those points, behind their own flag. This is real
metacode size, and the same size argument that applies to backtraces applies
here.

### Phase 3 - write W3S version 2

Program counters as (function index, Wasm byte offset). Slots as typed values,
little-endian regardless of host. References by index: a `funcref` as a function
index, an `exnref` as a tag index plus payload, a `contref` as a nested capture -
which is also what lets `frame_resume` be represented at last.

An `externref` belongs to the embedder, so it needs a pair of hooks to name and
re-bind it.

### Phase 4 - restore by re-materialisation

Re-instantiate, compile the functions the snapshot names, map each Wasm offset
to a local `pc`, place each typed value into the slot the destination's own map
assigns, rebuild the frame list and hand it to `ReplayFrames`. The rebuild path
already exists; what changes is where the frames come from.

### Phase 5 - draw the line at the host

Open descriptors, the working directory, preopens, clocks and entropy live
outside the Wasm state. Ship the honest subset first - nothing open beyond
preopens - and design the hook that lets an embedder serialize its own context.
Reopening files by path and offset is reasonable for regular files and wrong for
sockets and pipes, so it is the embedder's call and not the engine's.

## How to know it works

| Test | What it covers |
|---|---|
| Round trip | Snapshot at every safepoint, restore, run to the end, compare against the reference answer. One process, same build. Finds nearly everything. |
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

It recovers the endianness from the magic, and the slot width by trying both and
keeping whichever accounts for every byte. Its table of frame kinds past
`frame_try` is a guess, because the tail of the enum is conditional on the
build - one more argument for the build fingerprint in Phase 0.

## Scope

Migration becomes possible *at chosen points*, not at any instant. A program
blocked inside a host call never reaches one, which is the same boundary
`RunCodeChecked` already enforces by clearing `activeContinuation`: the native
frames on the far side of an import are not ours to describe.

Phases 0 to 4 are ordinary engineering with a visible finish line. Phase 5 does
not have one, and should be scoped deliberately rather than allowed to grow.
