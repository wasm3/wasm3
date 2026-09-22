# Example frames

One module per case, each with the frames a pause in it has to produce, written out by
hand. They pin down what the [Snapshot proposal](../../../docs/proposals/Snapshot.md)
says an activation holds, so any engine can check its own output against them;
`run-snapshot-format-test.py` checks Wasm3's.

Each module exports `run`, which the case calls with no arguments. The expectations are
comment lines in the source:

```
;; @pause interrupt
;; @frame <continuation> <function> <site> [loop@+<offset>] : [<local>...] | [<value>...]
```

- `@pause interrupt` pauses the way a pause the host requests does: at the first pause
  point reached. The runner steps on from there, resuming with the request set again,
  until the innermost frame of every continuation the lines name is the one its last
  line gives.
- `<continuation>` is `root`, `global:<n>` for the continuation global `n` holds, or
  `target` for the one the root's innermost `resume` runs.
- `<function>` is a function index. The `@frame` lines of a continuation are its
  activations, outermost first, and there are exactly that many.
- `<site>` is `back-edge`, `suspend`, `call`, `resume` or `entry`. A back edge also
  names the loop it goes to, by its offset in the function body the way `wasm_offset`
  counts it, as `snapshot-tool.py info` prints it: `loop@+0x3`.
- The locals are listed before `|`, and the operand stack after it, bottom first. A
  value is named by its type: `i32`, `i64`, `f32`, `f64`, `v128`, `funcref`,
  `externref`, `exnref` or `contref`.

The stack-switching cases are `.wast` scripts that carry their module's bytes, because
the bundled WABT predates the proposal; each shows the source it encodes.
