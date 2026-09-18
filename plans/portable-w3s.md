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
| pc to Wasm byte offset | `EmitMappingEntry` records one per emitted operation, `MapPCToOffset` reads it back - today only under `d_m3RecordBacktraces` |
| Native frames as data | `M3Frame` and `ReplayFrames`, from stack switching |
| Live slots at a safepoint | `RecordSafePoint`, from the compiler's `typeStack` and `wasmStack` - references only |
| References by name | `m3_snapshot.c` |

## Plan

### Phase 1 - type every live slot

Extend the safepoints from references to every live value: each live stack entry
with its type, the register's type, and which Wasm local or operand stack
position the entry is. The liveness rules already hold - a block's landing pads
are not values, and a back edge writes its loop's parameters - so this is a
matter of recording more of what `RecordSafePoint` already walks.

This is real map size, and the same size argument that applies to backtraces
applies here: behind its own flag.

### Phase 2 - write W3S

Program counters as (function index, Wasm byte offset). Values as Wasm values,
little-endian regardless of host, placed by Wasm local index and operand stack
position rather than by slot. Frames as Wasm constructs - a call, a block nest -
rather than as native frames.

An `externref` belongs to the embedder, so it needs a pair of hooks to name and
re-bind it.

### Phase 3 - restore by re-materialisation

Re-instantiate, compile the functions the snapshot names, map each Wasm offset to
a local pc and safepoint, place each value into the slot the destination's own
map assigns it, rebuild the frame list and hand it to `ReplayFrames`. The rebuild
path already exists; what changes is where the frames come from.

### Phase 4 - draw the line at the host

Open descriptors, the working directory, preopens, clocks and entropy live
outside the Wasm state. Ship the honest subset first - nothing open beyond
preopens - and design the hook that lets an embedder serialize its own context.
Reopening files by path and offset is reasonable for regular files and wrong for
sockets and pipes, so it is the embedder's call and not the engine's.

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
