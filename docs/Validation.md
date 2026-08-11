# Wasm3 module validation

This document describes how Wasm3 rejects invalid WebAssembly modules: where the
checks live, why they are arranged that way, which knobs a developer can turn,
and how the spec test suite exercises them.

## Two layers

Validation is split across two places, by the kind of thing being checked.

| Layer | File | Checks |
|---|---|---|
| **Structural** | `m3_parse.c` | Section order and uniqueness, LEB128 encoding limits, declared counts against sanity limits, index bounds (function / global / memory / table), memory and table limits, global mutability byte, start function signature, export name uniqueness, constant expressions |
| **Type** | `m3_validate.c` | Per-instruction operand types, control flow structure, block signatures, branch label types, polymorphic (unreachable) stack handling |

The split is not arbitrary: structural checks need module-wide context that only
the parser has (how many globals exist, whether a memory was imported), while
type checking needs a full operand-stack simulation over a function body and
nothing else. Keeping them apart means neither has to carry the other's state.

Each check lives in exactly **one** place. When the same rule could plausibly go
in either layer — memory access alignment, for instance — it belongs in the
validator, and the compiler does not repeat it. Duplicated checks drift apart.

The compiler is not a third layer. It maintains its own stack bookkeeping for
codegen and will incidentally reject some malformed bodies, but that behaviour
is a side effect and is not spec coverage. Never add a check there on the
grounds that it "also catches" something.

## Architecture

### Why a separate pre-pass validator

Wasm3's `typeStack[]` in `M3Compilation` is not a Wasm operand stack. It is part
of the register/slot allocator, and operations like `PreserveRegisterIfOccupied`
move values between slots and registers in ways that make the recorded types
diverge from the operand types the spec talks about. Type checking cannot be
layered onto it without extensive changes to codegen.

`m3_validate.c` therefore implements the validation algorithm from the spec
appendix over its own state, sharing nothing with the compiler:

```
ValCtx
  u8           opd  [d_m3ValStack]       operand type stack
  ValCtrlFrame ctrl [d_m3ValCtrlDepth]   control frames (block/loop/if)
  u8           localTypes [d_m3ValStack] params followed by declared locals
```

`c_valUnknown` (`0xFF`) is the spec's `Unknown` type, used after `unreachable`
and other stack-polymorphic points. `ValCtrlFrame.is_unreachable` marks a frame
polymorphic; `height` records the operand stack depth at block entry so the
stack can be truncated back to it.

The consequences that matter:

- **No coupling.** Whatever the register allocator does, the validation verdict
  is unaffected. This is what makes the validator safe to change.
- **Only invalid modules are rejected.** A valid module cannot start failing
  because of a codegen change.
- **The algorithm is the spec's**, so it can be checked against the spec text
  directly rather than against Wasm3's internals.

Alternatives considered: interleaving two parallel stacks inside the compiler
(WAMR's approach) requires the coupling described above; delegating to an
external validation library (wasmi delegates to `wasmparser`) has no viable C
equivalent for Wasm3's target platforms.

### When validation runs, and the lazy-compilation trade

`ValidateFunction` is called from `CompileFunction`, so it shares the lazy
compilation trigger: **a function body is validated the first time it is
compiled, not when the module is loaded.**

This deviates from the spec, which requires an invalid module to be rejected at
instantiation. It is a deliberate trade — validating every body at load would
walk all bytecode up front, costing cold-start latency and defeating the point
of lazy compilation on memory-constrained targets, and it would be paid by every
embedder whether they want it or not.

Embedders who want spec behaviour opt in by compiling everything up front:

```c
    m3_CompileModule (module);   // validates and compiles every function
```

The `wasm3` CLI exposes this as `--compile` ("disable lazy compilation"), which
applies both to a file named on the command line and to `:load` / `:load-hex`
issued in the repl.

Structural checks in `m3_parse.c` are **not** lazy — they always run during
`m3_ParseModule`, before any function body is touched.

### Constant expressions

Global initializers and data/element segment offsets are walked by
`Parse_InitExpr`, which reuses the compiler with `M3Compilation.isInitExpr` set.
That flag exists because a constant expression is not a function body and a few
rules differ — most importantly, `global.get` may only name an **imported**
global. A plain index bound check is not enough: a module's own globals are
appended to `numGlobals` before their initializer is walked, so an index check
alone would let a global initialize from itself.

Instructions that are not allowed in a constant expression are rejected earlier,
as `restricted opcode`.

## Knobs

### `d_m3EnableValidation` — `m3_config.h`, default `1`

```c
# ifndef d_m3EnableValidation
#   define d_m3EnableValidation                 1       // pre-pass bytecode type validation
# endif
```

Set to `0` to compile out the entire type-validation layer: `ValidateFunction`
becomes a stub returning `m3Err_none`, and `m3_validate.c` compiles to nothing.
Structural checks in `m3_parse.c` are unaffected and still run.

Disabling it means **trusting your input**. What stops being checked is
everything the validator solely owns — memory access alignment and memory-op
presence, for instance, are simply accepted. Note that a build with validation
disabled does *not* accept everything: the compiler keeps its own stack-height
bookkeeping and will still reject some malformed bodies as a side effect. That
is incidental and nowhere near spec coverage — do not treat it as a fallback.

Turning validation off is an appropriate trade for a device running a fixed,
pre-validated payload, and not for one loading modules from outside.

### `d_m3ValStack` / `d_m3ValCtrlDepth` — `m3_config.h`

```c
# ifndef d_m3ValStack
#   define d_m3ValStack         (d_m3MaxFunctionStackHeight)
# endif

# ifndef d_m3ValCtrlDepth
#   define d_m3ValCtrlDepth     ((d_m3MaxFunctionStackHeight)/8)
# endif
```

These size the validator's stacks and, with them, its memory cost. Both derive
from `d_m3MaxFunctionStackHeight` so the validator scales with the target's
other limits automatically — `d_m3ValStack` is the same operand stack the
compiler already bounds, and control frames are larger than operand entries, so
`d_m3ValCtrlDepth` dominates the total. Override either directly if the derived
value doesn't suit.

`ValCtx` is a **stack-allocated local** in `ValidateFunction`, so these decide
how much C stack the call needs:

| `d_m3MaxFunctionStackHeight` | `ValidateFunction` frame |
|---|---|
| 2000 (default) | ~10 KB |
| 128 (constrained platform configs) | ~830 bytes |

On a host the frame is irrelevant; on an MCU it may be the largest in the call
graph, and it now shrinks along with the platform config rather than staying at
its host size.

Exceeding either limit is reported as `m3Err_functionStackOverflow` — including
a function declaring more locals than `d_m3ValStack` — so lowering them trades
acceptance of large or deeply-nested functions for stack footprint. It never
truncates silently.

### `d_m3MaxSane*` — `m3_core.h`

Upper bounds on declared section counts (`d_m3MaxSaneFunctionsCount`,
`d_m3MaxSaneImportsCount`, `d_m3MaxSaneUtf8Length`, and so on). These are not
spec limits; they are a cheap guard so a corrupt or hostile count field cannot
drive a huge allocation before anything else notices. Lower them on constrained
targets.

### CLI flags

| Flag | Effect |
|---|---|
| `--compile` | Disable lazy compilation: compile (and therefore validate) every function at load, for both command-line and repl loads |
| `--spec-repl` | `--repl` plus `--compile`. What the spec test harness uses |

## Test harness

Validation is exercised by `test/run-spec-test.py` against the official
WebAssembly test suite, which it downloads on first run.

```sh
cd test
python3 run-spec-test.py                    # includes validation
python3 run-spec-test.py --no-validation    # functional assertions only
python3 run-spec-test.py --spec=v1.1
python3 run-spec-test.py --exec "../build/wasm3 --spec-repl"
```

### How invalid-module assertions are run

The suite's `assert_invalid`, `assert_malformed` and `assert_uninstantiable`
commands each name a module that must be rejected. For each one the harness
loads the module into a fresh runtime and requires an error.

Three deliberate choices:

- **Error text is not compared.** Wasm3's messages do not correspond to the
  spec's (`global index is too large` where the spec says `unknown global`), and
  forcing them to match would be churn for no safety. Only rejection is
  required; the expected text is recorded in the log for triage.
- **Text-format modules are skipped.** Some assertions carry `.wat` source
  rather than a binary; feeding those to Wasm3 would need a wat parser.
- **The module under test is restored afterwards.** A validation assertion loads
  a module of its own, so the harness reloads the file's current module before
  the next `invoke`.

### `--exec` must disable lazy compilation

The harness requires the module to be rejected **at load**. Because Wasm3
validates lazily, that only happens if the interpreter under test was started
with lazy compilation disabled — which is exactly what `--spec-repl` is for, and
why it is the default `--exec`.

If you pass your own `--exec`, use `--spec-repl` rather than `--repl`. With a
bare `--repl` the validation assertions fail in bulk, because every invalid
function body loads cleanly and is only caught later. The failure is loud by
design: a silent fallback would have hidden the fact that the run was not
testing what it claimed to.

### Blacklist

Known-failing and deliberately-deviating assertions are listed in the
`blacklist` near the top of `run-spec-test.py`, each with a comment explaining
why. Entries are `fnmatch` patterns over a test id:

```
<wast>:<line> <module>.wasm <assert type> (<expected error text>)
```

The expected error text is part of the id on purpose: it lets an entry name a
whole class of check, which survives module filenames like `binary.57.wasm`
being renumbered when the test suite is regenerated.

```python
  # a section whose declared size doesn't match its contents is accepted:
  # the section parsers don't report how many bytes they consumed
  "binary.wast:* * assert_malformed (section size mismatch)",
```

A blacklist entry should always say whether it is a gap to be fixed or an
intentional deviation. For example, wasm3 implements multi-value while the older
v1.1 suite still expects multi-result function types to be rejected — that entry
is marked as *not* a gap.

### NaN comparison

Floating-point results are compared **by NaN class**, not by exact bits. The
spec leaves the sign of a produced NaN non-deterministic, so the harness
classifies a NaN as canonical / arithmetic / signaling from its payload and
ignores the sign. A canonical NaN satisfies an `arithmetic` expectation.

One tolerance is encoded there: Wasm3 keeps all floats in a single `f64`
register (`_fp0`, see `m3_exec_defs.h`), so an **f32 signaling NaN is quieted**
by the `float -> double -> float` round trip, even through operations that
should preserve bits. The harness accepts a quieted result where an f32
signaling NaN was expected. The reverse — a signaling NaN where the spec
requires an arithmetic one — is still a failure, and f64 stays strict.

## Known gaps

- **UTF-8 in names is not validated.** Import, export and custom-section names
  are not checked for well-formed UTF-8. This is the largest remaining gap and
  is self-contained: one check at every name decode.
- **Section size mismatch is not detected.** A section whose declared size does
  not match its contents is accepted, because the section parsers do not report
  how many bytes they consumed. Fixing it means changing the `M3Parser`
  signature to report an end position; note that `ParseSection_Element`
  deliberately does not consume its section, so a blanket "must consume all"
  rule would not work.
- **The start function runs on first call, not at instantiation**, so a module
  whose start function traps is not rejected at load.
- **f32 signaling NaNs are quieted** by the shared `f64` register, as described
  above. Fixing it needs a separate f32 register or bit-exact slot storage.

## Adding a check

1. Decide the layer. Does it need module-wide context (`m3_parse.c`) or an
   operand-stack simulation (`m3_validate.c`)? If a rule seems to fit both,
   prefer the validator and do not duplicate it in the compiler.
2. Add the check in one place. In the parser use `_throwif`; in the validator
   return the error directly.
3. Confirm it fires. Load a module that violates the rule and check it is
   rejected — `wasm3 --spec-repl bad.wasm`.
4. Run the suite for both spec revisions, and the WASI tests. A new check that
   is too strict shows up as previously-passing functional assertions failing:

```sh
cd test
python3 run-spec-test.py
python3 run-spec-test.py --spec=v1.1
python3 run-wasi-test.py
```

5. If the check closes a blacklisted gap, remove that entry.

To investigate a single failure, `spec-test.log` records every assertion with
its outcome, and `--line <n>` re-runs one assertion from a `.wast`.
