# Suspendable execution and snapshots

`--snapshot <fn>` lets a running module return control to the host and continue
later from the same point. The CLI saves the suspended state to that file and exits;
`--resume <fn>` loads it in a new process.

A snapshot can be saved as a standalone file (`.dmp`) or embedded directly into a
WebAssembly module (`.wasm`) as a custom section with `--snapshot <out.wasm>[:<name>]`.
An embedded module can be resumed directly with `--resume <out.wasm>`.

This needs `d_m3HasStackSwitching`, and saving or loading snapshots also needs
`d_m3HasSnapshots`.

## Pause with a keyboard interrupt

With `--snapshot <fn>`, a keyboard interrupt requests a checkpoint:

| Platform | Shortcut | Interrupt |
|---|---|---|
| Linux / macOS / other POSIX systems | **Ctrl+C** | `SIGINT` |
| Linux / macOS / other POSIX systems | **Ctrl+Z** | `SIGTSTP` |
| Windows console | **Ctrl+C** or **Ctrl+Break** | Console control event |

When execution reaches a suspension point, the CLI writes the snapshot, prints
its path and exits. Start it again with `--resume <fn>` to continue the computation.

Create `count.wat`, a loop that counts to one billion:

```wat
(module
  (memory 1)
  (global $count (mut i32) (i32.const 0))
  (func (export "run")
    (loop $again
      (global.set $count (i32.add (global.get $count) (i32.const 1)))
      (br_if $again (i32.lt_u (global.get $count) (i32.const 1000000000)))))
)
```

Assemble it with the bundled WABT tool, run it, then press **Ctrl+C**:

```sh
$ build/wasm3 test/wasi/wabt/wat2wasm.wasm --enable-all count.wat -o count.wasm
$ build/wasm3 --snapshot count.dmp --func run count.wasm
Execution suspended. Snapshot saved to count.dmp
```

The request takes effect at the next pause point: a loop's back edge, or the start
of a function's body. Every loop and every recursion passes one, so any program that
runs long enough to interrupt reaches one soon. A host import that is still running
must return first.
On Windows, a second keyboard interrupt while the request is pending falls through
to the default console handler and can terminate the process before a snapshot is
saved.

`--interrupt` makes the same request before the run starts, so the run pauses at the
first point it can:

```sh
$ build/wasm3 --interrupt --snapshot count.dmp --func run count.wasm
```

A resume goes on past the point it paused at, so resuming with `--interrupt` again
pauses at the next one: that steps a program from one pause point to the next.

Resume with the same module:

```sh
$ build/wasm3 --resume count.dmp count.wasm
```

The saved execution already contains the function and its arguments. `--resume <fn>`
enables suspension automatically. If interrupted again, it overwrites the input
snapshot, unless `--snapshot <fn>` selects a different output. For an initial run,
`--snapshot <fn>` both enables suspension and selects its output file. A run that
completes normally does not save a snapshot.

### Embedded snapshots in WebAssembly binaries

Instead of keeping separate `.wasm` and `.dmp` files, you can embed the snapshot
directly into a `.wasm` file using `--snapshot <fn.wasm>`:

```sh
$ build/wasm3 --snapshot count_checkpoint.wasm --func run count.wasm
Execution suspended. Snapshot (embedded) saved to count_checkpoint.wasm
```

Saving again over the same file replaces the snapshot rather than adding one, so a
program can checkpoint itself as often as it likes without the binary growing. The
new file is written beside the old one and moved over it, so a save that fails part
way leaves the old file as it was.

#### Automatic Resumption

A module that carries an unnamed `snapshot` section resumes from it when it is run,
with no `--resume`:

```sh
$ build/wasm3 count_checkpoint.wasm
```

`--resume none` starts cold from `_start` instead. Naming a function with `--func`
also skips the snapshot and runs that function on a fresh instance.

#### Named snapshots

One binary can hold several snapshots under different names. `<file>.wasm:<name>`
selects one, wherever a file is expected:

```sh
# saves to the custom section "snapshot.stage1"
$ build/wasm3 --snapshot app.wasm:stage1 --func run count.wasm

# resumes it
$ build/wasm3 --resume app.wasm:stage1
$ build/wasm3 app.wasm:stage1
```

A name on `--snapshot` or `--resume` wins over one on the file to run. The name
starts after the last `.wasm:` in the argument. `--resume <file>.wasm` names the
module to run as well as the snapshot, so it takes no other file.

Two sections with the same name do not stop the module from loading or from running
cold. Only selecting that name fails.

## Pause when gas runs out

Combine suspension with a gas limit to checkpoint after a bounded amount of work:

```sh
$ wasm3 --gas-limit 100 --snapshot fib.dmp --func fib test/lang/fib32.wasm 24
$ wasm3 --gas-limit 10000 --resume fib.dmp test/lang/fib32.wasm
```

With `--snapshot <fn>`, running out of gas pauses instead of trapping. It is a request
to pause, like a keyboard interrupt, so the run goes on to the next pause point and
spends a little past its budget on the way. Each resume gets a fresh budget from
`--gas-limit`; the snapshot does not store the gas counter, and does not care whether
the run that resumes it is metered at all - leave `--gas-limit` off and the rest runs
to the end. The resumed CLI run does not print the function's return value.

## Move it to another machine

A snapshot is written in WebAssembly's terms rather than the interpreter's: where each
function stands is an offset into its Wasm body, and what it holds is its locals and
operand stack as typed values. The build that resumes it compiles the same module and
puts each value wherever *its* code keeps it. So a snapshot saved on one machine
resumes on another, with a different architecture, byte order, pointer or slot width
(`d_m3Use32BitSlots`), compiler or operating system:

```sh
$ build/wasm3 --gas-limit 100 --snapshot fib.dmp --func fib test/lang/fib32.wasm 24
$ qemu-s390x-static build-cross/wasm3-linux-s390x --resume fib.dmp test/lang/fib32.wasm
```


A `--resume` run does not pass the program its arguments again. A program reads them
from WASI once, at startup, and keeps them in its own memory, so that only matters for
a snapshot taken before it got that far.

## Suspend and resume from C

Enable suspension **before compiling or finding functions**, so loops and function
bodies include the checks that service `m3_RequestSuspend`. For a loaded module
exporting a zero-argument function `run`, the host can use this sequence:

```c
m3_SetSuspendable(runtime, true);

IM3Function function = NULL;
M3Result result = m3_FindFunction(&function, runtime, "run");
if (result) return result;

/* Request a pause at the first pause point: the start of run. */
m3_RequestSuspend(runtime);
result = m3_Call(function, 0, NULL);
if (result == m3Err_continuationSuspended) {
    /* Service other work here, then continue the same invocation. */
    result = m3_ResumeRuntime(runtime);
}
return result;
```

`m3Err_continuationSuspended` is a control result the host must handle separately
from traps. A resume can suspend again. `m3_IsSuspended(runtime)` reports whether
there is a paused invocation. A paused invocation keeps its place: a call the host
makes before `m3_ResumeRuntime` runs on a context of its own and leaves the paused
one untouched. For gas-driven scheduling, set the first budget with
`m3_SetResourceLimit(runtime, c_m3Limit_GasUnits, units)` before compilation,
and replenish it before each resume. A gas is `M3_GAS_UNITS_PER_GAS` units.

Resource limits are host configuration and are not restored from snapshots.
The Meta section records required memory bytes, table elements, and active
continuation stacks. Restore checks these totals before applying guest state.
Insufficient limits return `m3Err_memoryLimitExceeded`, `m3Err_tableLimitExceeded`,
or `m3Err_continuationLimitExceeded`, leaving the module reusable: raise the
limit and retry on the same instance. Other modules' resource usage is included
when checking available headroom. Consumed continuation references cost no stacks.
CLI snapshot workflows accept `--max-memory`, `--max-table-elements`, and
`--max-continuations` alongside `--snapshot` or `--resume`.

A resume that runs to the end leaves the results where the call would have, so
`m3_GetResults` on the function that was called reads them.

To persist a paused invocation, call
`m3_SaveSnapshotToBuffer(runtime, &bytes, &size)`, with `void *bytes = NULL` and
`size_t size = 0`, and check the result. Restore into a fresh runtime: enable
suspension, parse and load the original module, link its host imports, set a gas limit
if this runtime should have one, then call
`m3_LoadSnapshotFromBuffer(runtime, module, bytes, size)`.
The module has to be freshly instantiated: once any of its code has run, its start
function included, the load is refused. After a successful load, call
`m3_ResumeRuntime`. A load that fails after it started changing the module leaves it
unusable: it refuses to run, resume, be saved or take another snapshot, so free the
runtime and start again. Release the allocated snapshot
buffer with `free(bytes)` when finished with it. Every function the snapshot names
has to be compiled after suspension is enabled; one compiled before it is refused.

A snapshot can also ride inside the module it belongs to. `name` selects one, and
NULL or `""` means the unnamed default:
- `m3_HasSnapshot(module, name)` says whether the module carries one.
- `m3_GetEmbeddedSnapshot(module, name, &bytes, &size)` points at it. The bytes belong
  to the module and live as long as the binary it was parsed from.
- `m3_LoadEmbeddedSnapshot(runtime, module, name)` restores it.
- `m3_SaveSnapshotToModule(runtime, module, name, &out_bytes, &out_size)` writes the
  module's bytes with the paused invocation embedded in them, replacing any snapshot of
  the same name already there. With nothing paused it fails: a postmortem is never
  embedded. Release `out_bytes` with `free`.

`m3_SaveSnapshot` and `m3_LoadSnapshot` instead stream through writer and reader
callbacks. Each callback must transfer exactly the requested byte count and return
`m3Err_none`, or return an error. See the
[`snapshot.*` embedding tests](../test/internal/m3_test.c) for complete examples
that save a loop's progress and restore it into a new runtime.

## What a snapshot preserves

A snapshot records the entry module's linear memories, globals, tables and dropped
segments, together with the paused call and every continuation and exception it can
still reach. A memory or table the module imports at two indices is recorded once, and
restores only into an instance whose imports share it the same way.
[Snapshot proposal](proposals/Snapshots.md) specifies the binary layout, which references
can be saved and which are refused, etc.

Host state is not part of it unless the embedder puts it there. `m3_SetSnapshotHooks`
hands the runtime an `M3SnapshotHooks` with up to four callbacks: `nameExternRef` and
`bindExternRef` turn an `externref` into a number the embedder can resolve again in
another process, and back; `saveHostState` and `loadHostState` carry the embedder's own
bytes - open files, clocks, whatever its imports keep - along with the program's. A
runtime without `nameExternRef` refuses to save a non-null `externref`, and one without
the loading half refuses a snapshot that needs it. Which host state can come across is
the embedder's call: reopening a file by path and offset is reasonable for a regular
file and wrong for a socket or a pipe. The CLI sets no hooks, so a program it resumes
must not depend on anything it opened beyond standard input and output and the preopened
directories.

[`extra/snapshot-tool.py`](../extra/snapshot-tool.py) reads that
format independently of Wasm3: `info` summarizes a snapshot, `verify` checks its
structure, `unpack` and `pack` turn it into JSON plus binaries and back, and `embed` /
`extract` manage embedded snapshots in `.wasm` binaries.

`--dump-on-trap` writes `wasm3_dump.dmp` after a trap. It is a postmortem snapshot:
it records state for inspection, but cannot be resumed. It does not enable
suspension, so running out of gas still traps. From C, `m3_SaveSnapshot` on a
runtime with no paused invocation writes the same postmortem for the module the
last call entered. It fails if no call has entered any module.

