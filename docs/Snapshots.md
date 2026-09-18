# Suspendable execution and snapshots

`--snapshot <fn>` lets a running module return control to the host and continue
later from the same point. The CLI saves the suspended state to that file and exits;
`--resume <fn>` loads it in a new process.

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
$ build/wasm3 --snapshot count.w3s --func run count.wasm
Execution suspended. Snapshot saved to count.w3s
```

The request takes effect at the next instrumented loop back edge. Code without
loop back edges may finish without servicing the request, and a host import that
is still running must return before guest execution can reach a suspension point.
On Windows, a second keyboard interrupt while the request is pending falls through
to the default console handler and can terminate the process before a snapshot is
saved.

Resume with the same module:

```sh
$ build/wasm3 --resume count.w3s count.wasm
```

The saved execution already contains the function and its arguments. `--resume <fn>`
enables suspension automatically. If interrupted again, it overwrites the input
snapshot, unless `--snapshot <fn>` selects a different output. For an initial run,
`--snapshot <fn>` both enables suspension and selects its output file. A run that
completes normally does not save a snapshot.

## Pause when gas runs out

Combine suspension with a gas limit to checkpoint after a bounded amount of work:

```sh
$ wasm3 --gas-limit 100 --snapshot fib.w3s --func fib test/lang/fib32.wasm 24
$ wasm3 --gas-limit 10000 --resume fib.w3s test/lang/fib32.wasm
```

With `--snapshot <fn>`, exhausting gas suspends execution instead of trapping.
Each resume gets a fresh budget from `--gas-limit`; the snapshot does not store
the gas counter. Gas metering has to be on for the restore exactly when it was on
for the save, because its instrumentation changes the compiled code; a snapshot
records which it was and is refused otherwise. The resumed CLI run does not print
the function's return value.

## Suspend and resume from C

Enable suspension **before compiling or finding functions**, so loops include
the checks that service `m3_RequestSuspend`. For a loaded module exporting a
zero-argument function `run` with a loop, the host can use this sequence:

```c
m3_SetSuspendable(runtime, true);

IM3Function function = NULL;
M3Result result = m3_FindFunction(&function, runtime, "run");
if (result) return result;

/* Request a pause at the first loop back edge. */
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
`m3_SetGasLimit` before compilation, and replenish it before each resume.

A resume that runs to the end leaves the results where the call would have, so
`m3_GetResults` on the function that was called reads them.

To persist a paused invocation, call
`m3_SaveSnapshotToBuffer(runtime, &bytes, &size)`, with `void *bytes = NULL` and
`size_t size = 0`, and check the result. Restore into a fresh runtime: enable
suspension and the same metering mode, parse and load the original module, link its
host imports, then call `m3_LoadSnapshotFromBuffer(runtime, module, bytes, size)`.
After a successful load, call `m3_ResumeRuntime`. Release the allocated snapshot
buffer with `free(bytes)` when finished with it. Every function the snapshot names
has to be compiled after suspension is enabled; one compiled before it is refused.

`m3_SaveSnapshot` and `m3_LoadSnapshot` instead stream through writer and reader
callbacks. Each callback must transfer exactly the requested byte count and return
`m3Err_none`, or return an error. See the
[`snapshot.*` embedding tests](../test/internal/m3_test.c) for complete examples
that save a loop's progress and restore it into a new runtime.

## What a snapshot preserves

A snapshot records the entry module's linear memories, globals, tables and dropped
segments, together with the paused call and every continuation and exception it can
still reach. Host state is not part of it. [W3S file format](W3S.md) specifies the
binary layout, which references can be saved and which are refused, etc.

[`extra/w3s-tool.py`](../extra/w3s-tool.py) reads that
format independently of Wasm3: `info` summarizes a snapshot, `verify` checks its
structure, `unpack` and `pack` turn it into JSON plus binaries and back, and `diff`
compares two.

`--dump-on-trap` writes `wasm3_dump.w3s` after a trap. It is a postmortem snapshot:
it records state for inspection, but cannot be resumed. It does not enable
suspension, so running out of gas still traps. From C, `m3_SaveSnapshot` on a
runtime with no paused invocation writes the same postmortem for the module the
last call entered.
