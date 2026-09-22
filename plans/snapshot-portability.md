# Snapshot portability: frames, pause points and back edges

The [Snapshot proposal](../docs/proposals/Snapshot.md) says a snapshot is written in
Wasm's terms, so any conforming engine can resume one. Three parts of the activation
record still depend on how Wasm3 compiles and runs code:

- **What a frame holds.** The proposal says "live operand stack", but never says, for
  each kind of pause point, which values that is.
- **Where a frame can stop.** Back edges are told apart by an `ordinal` in Wasm3's
  emission order. Gas pause points sit where Wasm3's metering happens to open a
  segment. Whether a `return_call` leaves its caller standing depends on the compiler
  Wasm3 was built with.
- **What stands around a frame.** `blocks` and `handlers_live` describe the
  interpreter's native frames, not anything the Wasm program can observe.

This plan makes all three portable. Version 1 is amended in place: there is no
version 2. The work that changes the wire format happens in one step, so existing
snapshots stop being readable once, not three times. Nothing in the repository is a
committed `.dmp`: every test builds its snapshots from `.wat` sources, so the break
costs nothing here.

## Decisions

Taken:

- **Gas leaves the format.** Metering is an engine mechanism for counting and limiting
  work, not part of Wasm. Running out of gas becomes a pause request, handled at the
  next pause point, not a pause at the exact instruction where the budget ran out. The
  snapshot never records the gas counter, so nothing in the file depends on where the
  budget ran out.
- **Function entry becomes a pause point**, taking kind `4` from `gas`. Without it,
  code with recursion but no loops, such as [fib32](../test/lang/fib32.wat), has
  nowhere to pause at all. With it, every cycle a program can run passes a pause point:
  a loop passes its back edge, and recursion passes a function entry.
- **Back edges are named by their target loop.** `ordinal` becomes `target_loop`, the
  `wasm_offset` of the loop the branch goes to - the same kind of name every other
  place in the format uses, and one a reader can check: it has to be a loop around the
  instruction that the instruction branches to.
- **No version 2.** The format changes in place.
- **`blocks` and `handlers_live` leave the format.** The reader works them out from its
  own compilation: the loops and `try_table` regions that enclose the pause point, and
  which of those the branch there leaves. That is a function of the pause point. The
  native frames standing are not - they also hold regions the function has already
  left, and which of those depends on the path it took (see phase 0).
- **A tail call leaves no frame behind.** A producer never writes a frame waiting at a
  `return_call`, and a reader places the callee where that caller's frame began. The
  caller only ever passed its callee's results on, and a `return_call` callee returns
  the caller's result types, so nothing is lost by leaving it out. No reader has to
  rebuild it - and none could, since the file no longer says it existed. Requiring
  engines to reuse the frame instead is not an option: a Wasm3 built without
  `musttail` cannot, and neither can any build across modules.

## Phase 0: the facts the later phases rest on

Done. What it established:

**Tail calls.** A snapshot paused under a `return_call` is written today only by a
build that compiles the tail call as a call and a return (gcc, no `musttail`), and
resumes only in such a build. The module:

```wat
(module
  (global $n (mut i32) (i32.const 0))
  (func $spin (result i32)
    (loop $l
      (global.set $n (i32.add (global.get $n) (i32.const 1)))
      (br_if $l (i32.lt_u (global.get $n) (i32.const 1000))))
    (global.get $n))
  (func (export "run") (result i32)
    (return_call $spin)))
```

- gcc writes `run` waiting at the `return_call` (a call safepoint), then `$spin`.
- clang cannot save it: `SaveSuspendedBody` takes each activation's function from the
  entry function or the call frame's callee, and `$spin`'s pause point is in neither.
  The same holds for a tail call below the outermost frame.
- clang refuses gcc's snapshot: it has no call safepoint at the `return_call`.
- A tail-called function in another module cannot be written by either build, since a
  snapshot covers one instance and the callee's frame belongs to another.
- A `return_call` inside a loop leaves the loop's native frame standing at the same
  stack position as the callee (`op_Loop` does not unwind on a tail jump), so a
  clang build's saving also has to drop those frames.
- CoreMark carries two `return_call`s (functions 11 and 12, calling 17 and 16).

**Native frames around a pause point.** The loop and `try_table` frames standing at a
pause point are the enclosing regions, in order, plus regions the function has already
left: a loop or `try_table` exited by falling out of its end or by a forward branch
keeps its native frame until a back edge to an enclosing loop, or the function's
return, unwinds it. Which of those are standing depends on the path taken. Across
the embedding tests and both snapshot suites, 336 of 9,134 activations held such
frames, and the enclosing chain was an ordered subsequence of what stood in all
9,134. The leftover frames are inert: nothing branches back into a region that has
been left, and a retired `try_table` is walked past, so leaving them out at load
changes nothing.

**`handlers_live = 0`.** Over the enclosing regions it marks exactly the `try_table`s
that the branch at the pause point leaves - a branch, a catch clause, or an `on`
clause, whose label lies outside them. Leftover `try_table` frames carry it too.
`op_PopHandlers` is emitted for all three kinds of branch; a catch clause's own
`try_table` has no frame left once its handler runs.

**Baselines** (WSL, Release, `-march=native`; same-binary control alongside):

| | suspendable / plain, 5 workloads | control | `fib32(38)` | control |
|---|---|---|---|---|
| clang 18 | 0.996x | 0.995x | 0.967 s, 1.0007x | 0.9995x |
| gcc 13 | 1.000x | 1.002x | 0.995 s, 0.9950x | 1.0012x |

The workloads are CoreMark, c-ray, mandel, smallpt and mal-fib. Interpreter `.text`
(non-LTO objects): `m3_compile` 102,626 / 105,040 (clang / gcc), `m3_snapshot` 33,793 /
45,131, all of `m3` 226,880 / 255,372. Suspendable code is the same size as plain;
what it adds is the snapshot maps, and gas pause points are about 85% of them:

| Program (clang) | Metacode bytes | Pause points | of which gas | Map bytes |
|---|---|---|---|---|
| CoreMark | 162,568 | 2,537 | 2,155 | 86,072 |
| mandel | 203,104 | 2,813 | 2,427 | 94,644 |
| brotli | 2,171,952 | 24,074 | 21,081 | 821,304 |
| `fib32` | 264 | 7 | 5 | 232 |

## Phase 1: what a frame holds (C1)

The wire format stays the same: no field is added or removed. What changes is what the
spec says the fields hold, the tests that check it, and - for tail calls - which frames
Wasm3 writes.

The operand stack at each kind of pause point, as the Wasm validator sees it:

| Kind | Stack in the frame |
|---|---|
| `0` back edge | The stack as it was when the target loop was entered, then the loop's parameters, which the branch has just written. Values pushed inside the loop are dropped, and so is a `br_if` condition. |
| `1` suspend | The stack just after the `suspend` or `switch`, its results included. |
| `2` call | The stack below the call's operands. Arguments are consumed and results are not there yet. |
| `3` resume | The stack below the operands of `resume`, `resume_throw` or `resume_throw_ref`. Arguments, the continuation reference and any exception are consumed. |
| `4` entry | Empty. The locals hold the arguments and each declared local's default value. |

Throughout:

- "Every value on the stack" replaces "live operand stack".
- A block's parameters are ordinary values on the stack.
- The locals come first, in declaration order.
- The Security invariant that types "match the validator at that instruction" is
  rewritten to follow this table.

Until phase 2 takes them out, `blocks` lists exactly the loops and `try_table` regions
that enclose the pause point, outermost first, and `handlers_live` is 0 exactly for a
`try_table` that the branch at the pause point leaves. Wasm3's saving writes that,
leaving out frames of regions the function has already left.

Tail calls:

- A `return_call*` replaces the caller's frame, so no activation is ever stopped at
  one.
- The outermost activation of a continuation is whatever function holds its entry
  frame now. The rule that its `func_index` equals `entry_func_idx` is dropped.
- A producer that implements `return_call` as a call and a return leaves the waiting
  caller out.
- A reader places the callee's frame where the left-out caller's frame began: at the
  continuation's base, or where the outer call safepoint says its callee starts. When
  the callee returns, its results land where the caller's would have, since a
  `return_call` requires the callee's result types to match the caller's. The reader
  rebuilds nothing.
- `entry_func_idx` still names the function the continuation was made with, or the
  function the host invoked.
- What replaces the dropped rule is a check on result types. The outermost activation's
  function returns what the continuation returns: the entry function's results, or the
  continuation type's. Each activation below a call safepoint returns what that call
  expects. Today the loader checks neither: it trusts that the next activation is the
  function the call named, which a tail call makes untrue.

In Wasm3:

- The compiler keeps recording the call site of a `return_call` that it compiles as a
  call, marked as a tail call, because in those builds the caller is still a real
  native frame that saving has to walk past. It is never written and never matched on
  load.
- Saving skips a frame stopped at a marked site, and carries on to the callee at the
  position the callee's frame actually has. Where the tail call reused the frame,
  saving takes the function from the frame itself rather than from the call that
  entered it, and drops the loop and `try_table` frames the replaced caller left at
  that position.
- Loading drops the `a == 0 and function != entryFunction` check and adds the
  result-type checks.
- The tool's `verify` stops checking the outermost activation against the entry
  function.

**Example frames.** `test/snapshot/frames/*.wat`, one module per case, each paired with
the frame it has to produce, written out by hand as types and counts:

- a call inside a block that takes parameters
- a suspend with results
- a `resume`, and a `resume_throw_ref`
- a back edge from a `br_table`, to loops that take parameters
- a catch clause that branches back to a loop
- an `on` clause that branches back to a loop outside a `try_table`
- a back edge after a loop and a `try_table` the function has already left
- a tail-call chain

The format test checks what Wasm3 writes against those lists, and another engine can
check its own output against the same ones. The CLI's `--interrupt` requests a pause
before the run starts, which is what stops each example at an exact pause point: the
first one it reaches, and after phase 3, each next one in turn when a resume carries
the flag again.

## Phase 2: the format change

Everything that changes the wire format lands here, together.

1. **`target_loop` replaces `ordinal`.** It is present only when `site_kind == 0`.
   - **Compiler:** each compilation scope keeps the offset of its opening opcode, and
     `RecordSafePoint` stores the target loop's offset with each back edge.
   - **Saving:** writes that offset. `SafePointOrdinal` goes.
   - **Loading:** `FindSafePointAt` matches on `(wasm_offset, kind, target_loop)` and
     takes the first match. Two catch or `on` clauses that name the same loop leave the
     same frame, so either of their pause points is correct.
   - **Loading checks:** the loop exists and encloses the instruction. An instruction
     that cannot branch there has no matching pause point.
2. **Kind `4` becomes `entry`.** `safepoint_gas` becomes `safepoint_entry`, keeping
   the number, and `M3SafePointKind` says what the number means.
3. **`blocks` and `handlers_live` leave the activation.** The compiler records, with
   each pause point, the loops and `try_table` regions that enclose it and which of
   those the branch there leaves. Loading rebuilds the native frames from that, and
   saving no longer walks them.
4. **Tool.**
   - Read and write `target_loop`.
   - Rename `SITE_GAS` to `SITE_ENTRY`.
   - Drop `blocks`.
   - `info` shows where each back edge is headed, for example
     `at back edge +0x23 -> loop@+0x8`.
   - The `unpack` JSON follows the fields.
5. **Spec.** The activation grammar, the kinds table, restoration step 4 and the
   validation invariants all change. Fold the back-edge amendment into
   [Snapshot.md](../docs/proposals/Snapshot.md) and delete the separate document.
   Remove every mention of gas: the kinds table, the pause-site list in the abstract
   model, and the sentence about unmetered engines.
6. **Regression cases.**
   - The `br_table` module from the back-edge amendment. Resuming from a pause at
     either loop gives 604, and a `target_loop` changed to the other loop never
     silently resumes into it.
   - A `target_loop` that names a block, a loop that does not enclose the branch, or a
     loop the branch cannot reach is rejected.

## Phase 3: where a frame can stop (C2)

The wire format stays the same here. This phase is about the spec and the engine.

**The spec lists every pause point:**

| Kind | Where |
|---|---|
| `0` back edge | Every branch to a `loop` label: `br`, `br_if`, `br_table`, the `br_on_*` family. A catch clause's is recorded at its `try_table`, and an `on` clause's at its `resume` or `resume_throw`. |
| `1` suspend | `suspend`, `switch` |
| `2` call | `call`, `call_indirect`, `call_ref`, but never `return_call*` |
| `3` resume | `resume`, `resume_throw`, `resume_throw_ref` |
| `4` entry | Before the first instruction of every function body. Its `wasm_offset` is the offset of that instruction, just past the local declarations - which is the body's `end` when the body is empty. |

**What the spec says about pausing:**

- A pause starts only at kinds `0`, `1` and `4`. Kinds `2` and `3` are where an outer
  frame waits while something deeper is paused.
- Resuming from a kind `0` or `4` point goes past it: a pause request that is already
  set when execution continues takes effect at the next pause point, not at the one
  just resumed from. Otherwise a request made before resuming would pause again on the
  spot, with no progress.
- A pause requested by the host takes effect at the next kind `0` or `4` point. How
  long that takes is up to the engine, but it is bounded: every cycle a program can run
  passes one of them.
- A host import that is still running must return first.

**Engine.**

- **Function entry.** When the runtime is suspendable, a body opens with
  `op_Entry_Suspendable`: `op_Entry` with a pause-request check once it has zeroed the
  locals and copied the constants. It pauses with its pc at the body's first
  operation, where the `entry` pause point is recorded, so going on from it is going
  on past the check. Saving writes the locals and an empty stack. A build whose
  `op_Entry` keeps a native frame (backtraces, structured traces) cannot fold the
  check in, and emits `op_EntryCheck` after `op_Entry` instead. A function-entry pause
  joins the phase 1 example frames.
- **Gas.** `op_UseGas`, when there is not enough gas for the segment:
  - In a runtime that is not suspendable, it traps, as now.
  - In a suspendable one, it sets `suspendRequested` and lets the segment run, so the
    counter may go below zero until the next pause point takes the request. Each resume
    starts from a fresh budget, as now.
- **Metering is no longer tracked for snapshots.** `MeterOpcode` no longer records
  pause points. Its tracking path for snapshots, which followed segments even when not
  metering, goes. It meters or it does nothing.
- **Resuming goes past the point it resumed from.** A back edge pauses in
  `op_ContinueLoop_Suspendable` or its conditional form, and nothing clears
  `suspendRequested` on the way back in. So `m3_ResumeRuntime` sets
  `resumePastCheck`, and the first suspension point the replay comes down to - the
  one the runtime paused at - spends it: at a back edge, the replay runs the plain
  `op_ContinueLoop` or `op_ContinueLoopIf` on the same immediates instead of the
  check. A fused entry is past its check already, and kinds `1`, `2` and `3` resume
  past their instruction. No check pays for this: the checks do not read the flag.

**Checking the placement.** A Python script reads `wasm-objdump -d` output from the
tree's tools and works out every pause point from the instruction stream alone. An
embedding test walks each function's snapshot map and compares, over the phase 1 example
modules and the programs `run-snapshot-test.py` carries (mandelbrot, smallpt,
CoreMark).

## Phase 4: tests, docs and a cross-build run

**Tests that stop at gas points move to stepping.** Calling `m3_RequestSuspend` again
before every `m3_ResumeRuntime` pauses the program at every back edge and function entry
it passes, now that each resume goes past the point it stopped at. That reaches more
points than the gas stops did, through the public API alone. A step that stops where it
started fails the test, so a regression in going past shows up as a failure, not a
hang.

- `round_trip_at_every_gas_stop` becomes a round trip at every pause point.
- `gas_stop_resumes_unmetered` becomes a check that a gas-driven pause resumes in a
  runtime that is not metering.
- In the format test, the fixtures that collect stages with `--gas-limit 1..250` keep
  using gas, which now pauses at the next pause point. Check that `bind.wast` still
  reaches all six stages and `references.wat` both of its own.
- `run-snapshot-test.py` keeps its `--legs` gas budgets. The legs fall in different
  places, and each still has to resume to the same final result.
- Add a gas budget on recursive `fib`. It has to pause, which today's back edges alone
  cannot do.

**Docs.**

- [Snapshots.md](../docs/Snapshots.md): running out of gas pauses at the next pause
  point. A keyboard interrupt now reaches recursive code as well, so the warning that
  code without back edges may finish without pausing goes.
- The `m3_SetGasLimit` and `m3_RequestSuspend` comments in `wasm3.h` say the same.
- The tool's docstring follows its fields.

**Cross-build run.** `run-snapshot-test.py` between a gcc build and a clang build (frame
reuse against none), on top of the 32-bit-slot and s390x pairings it already covers.
Then `extra/check.py` and the warning matrix from AGENTS.md.

## Measurements

- **Function-entry check.** It costs one load and one branch per call, and only in code
  compiled while the runtime is suspendable. Measure it against the phase 0 baseline
  with a same-binary control. Recursive `fib` is the worst case. Code that is not
  suspendable must not change at all.
- **Code size.** Removing the gas pause points shrinks the snapshot maps; the entry
  check adds one op per suspendable function. Measure interpreter `.text` and generated
  code separately, since they move for different reasons.

## Done when

- Every field of an activation is defined by the module and the program's Wasm state
  alone: no emission order, no gas, no native frames.
- A snapshot written by the gcc build resumes in the clang build and the other way
  round, with a tail call standing in the paused program.
- Pausing at every pause point of the snapshot programs resumes to the same result, and
  every step moves forward.
- A gas budget on recursive `fib` pauses and resumes.
- The spec has no mention of gas, of `ordinal`, or of anything to do with registers.
