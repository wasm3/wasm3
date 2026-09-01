# Wasm3 module validation

This document describes how Wasm3 rejects invalid WebAssembly modules: where the
checks live, why they are arranged that way, which knobs a developer can turn,
and how the spec test suite exercises them.

## Two layers

Validation is split across two places, by the kind of thing being checked.

| Layer | File | Checks |
|---|---|---|
| **Structural** | `m3_parse.c` | Section order and uniqueness, LEB128 encoding limits, declared counts against sanity limits, index bounds (function / global / memory / table), memory and table limits, page sizes, global mutability byte, start function signature, export name uniqueness, constant expressions |
| **Type** | `m3_validate.c` | Per-instruction operand types, control flow structure, block signatures, branch label types, polymorphic (unreachable) stack handling |

The split is not arbitrary: structural checks need module-wide context that only
the parser has (how many globals exist, whether a memory was imported), while
type checking needs a full operand-stack simulation over a function body and
nothing else. Keeping them apart means neither has to carry the other's state.

Each check lives in exactly **one** place. When the same rule could plausibly go
in either layer - memory access alignment, for instance - it belongs in the
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
instantiation. It is a deliberate trade - validating every body at load would
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

Compiling everything up front is also what the CLI's `--validate-only <file>`
does: it loads the file, compiles every function, runs none of them, and exits 0
if all of that succeeded.

```sh
wasm3 --validate-only module.wasm && echo valid
```

The verdict it reports is "this build can load and run the file", which is
slightly stronger than the spec's notion of validity - the module is also
instantiated, so an unsatisfied import or a trapping start function fails it too.
In a build with `d_m3EnableValidation` off the flag refuses to answer at all
rather than passing everything, since nothing in that build type checks a body.

The opposite opt-out is `m3_SetValidation (runtime, false)`, which leaves the
pre-pass out of compilation entirely - the CLI's `--no-validate`, which applies
to every runtime the process makes, `:init` included. The trade is the one
`d_m3EnableValidation` makes, decided per runtime rather than per build, so read
that knob's warning below before reaching for it.

Structural checks in `m3_parse.c` are **not** lazy - they always run during
`m3_ParseModule`, before any function body is touched.

### Constant expressions

Global initializers and data/element segment offsets are walked by
`Parse_InitExpr`, which reuses the compiler with `M3Compilation.isInitExpr` set.
That flag exists because a constant expression is not a function body and a few
rules differ - most importantly, `global.get` may only name an **imported**
global. A plain index bound check is not enough: a module's own globals are
appended to `numGlobals` before their initializer is walked, so an index check
alone would let a global initialize from itself.

Instructions that are not allowed in a constant expression are rejected earlier,
as `restricted opcode`. The permitted set is `*.const`, `global.get`, `ref.null`
and `ref.func`, plus - with `d_m3HasExtendedConst` - the `i32`/`i64` `add`, `sub`
and `mul` of the extended constant expressions proposal.

The walk only type-checks; nothing is emitted, because the module's code pages do
not exist yet. The expression is compiled and run for real later, during
instantiation, by `EvaluateExpression` in `m3_env.c`: it builds a throwaway
runtime, calls `CompileExpression` (the same root-block shape a function gets,
with one result and no args) and reads the value back out of slot 0.

### Memory limits

A memory's declared minimum and maximum are checked against `2^32/pagesize`
pages. A module that declares no page size gets the default of 65536, so the
bound is the familiar 65536 pages.

The page size itself is checked only for being a power of two no larger than
65536 - it arrives as a log2, so the encoding grants the first half of that.
This is deliberately wider than the custom page sizes proposal, which admits
only the endpoints `1` and `65536`: nothing in the engine cares which power of
two a page is, so there is nothing to gain by refusing the ones in between. The
suite's checks that they *are* refused are turned off in `run-spec-test.py`,
with the ceiling still exercised by the modules asking for `2^17` and `2^65`.

Note that `d_m3MaxLinearMemoryPages` is a *size*, counted in default-sized
pages - it is compared against the memory's byte length, not its page count, so
that lowering it constrains the same amount of memory whatever page size a
module asks for.

### Reference types

A value type is an `m3type_t`, not a byte. The plain `M3ValueType` values keep
their old encoding, so every comparison against a `c_m3Type_*` constant still
means what it did; a reference spelled out in full - `(ref $t)` or
`(ref null $t)` from the typed function references proposal - sets the top bit
and spends the rest on a null flag and the *canonical* index of the function
type it points at.

Canonical is the important word. `Environment_AddFuncType` already collapsed
structurally equal function types onto one `M3FuncType`, so numbering those
gives `$t <: $t'` exactly the meaning the spec asks for - the types are
equivalent - without comparing structures at each check. `IsSubTypeOf()` is the
only place that relation lives; `BaseTypeOf()` reduces any reference back to the
`funcref` or `externref` it is stored as, which is what slot sizing, the
operation tables and the public API all want.

The index has to fit alongside the flags, which is why `d_m3MaxSaneTypesCount`
is 8190 rather than something rounder, and why exceeding it is an error rather
than a silent truncation. With `d_m3HasTypedRefs` off there is nothing to spell
out, so `m3type_t` narrows back to a byte, the limit returns to a million, and
`IsSubTypeOf()` collapses to equality - the compiler's type stack, and with it
`M3Compilation`, are exactly the size they were before the proposal.

Note that `m3_GetArgType()` and `m3_GetRetType()` report the storage type: a
host sees `funcref` whatever shape the reference has, matching the proposal's
own position that concrete reference types don't cross the embedding boundary.

## Knobs

### `d_m3EnableValidation` - `m3_config.h`, default `1`

```c
# ifndef d_m3EnableValidation
#   define d_m3EnableValidation                 1       // pre-pass bytecode type validation
# endif
```

Set to `0` to compile out the entire type-validation layer: `ValidateFunction`
becomes a stub returning `m3Err_none`, and `m3_validate.c` compiles to nothing.
Structural checks in `m3_parse.c` are unaffected and still run.

Disabling it means **trusting your input**. What stops being checked is
everything the validator solely owns - memory access alignment and memory-op
presence, for instance, are simply accepted. Note that a build with validation
disabled does *not* accept everything: the compiler keeps its own stack-height
bookkeeping and will still reject some malformed bodies as a side effect. That
is incidental and nowhere near spec coverage - do not treat it as a fallback.

Turning validation off is an appropriate trade for a device running a fixed,
pre-validated payload, and not for one loading modules from outside.

### `d_m3ValStack` / `d_m3ValCtrlDepth` - `m3_config.h`

```c
# ifndef d_m3ValStack
#   define d_m3ValStack         (d_m3MaxFunctionStackHeight)
# endif

# ifndef d_m3ValCtrlDepth
#   define d_m3ValCtrlDepth     ((d_m3MaxFunctionStackHeight)/4)
# endif
```

These size the validator's stacks and, with them, its memory cost. Both derive
from `d_m3MaxFunctionStackHeight` so the validator scales with the target's
other limits automatically - `d_m3ValStack` is the same operand stack the
compiler already bounds, and control frames are larger than operand entries, so
`d_m3ValCtrlDepth` dominates the total. Override either directly if the derived
value doesn't suit.

They decide the size of one buffer:

| `d_m3MaxFunctionStackHeight` | `d_m3ValCtrlDepth` | `ValCtx` |
|---|---|---|
| 8000 (default) | 2000 | 62.5 KB |
| 2000 | 500 | 15.7 KB |
| 128 (constrained platform configs) | 32 | 1.0 KB |

That buffer belongs to the **runtime**, not to `ValidateFunction`'s C stack frame
(96 bytes). `M3Runtime.validator` is allocated the first time something in that
runtime is validated and reused for every function after it, so a runtime that
never compiles - or one running with `m3_SetValidation (rt, false)` - never pays
for it, and it is released with the runtime.

Keeping it there rather than on the stack is not a micro-optimisation. Validation
is a pre-pass of compiling, compiling is lazy, and lazy compiling fires from
`op_Call` arbitrarily deep in a native call chain - against the 128 KB of real
stack `d_m3MaxNativeStack` holds back past the point where Wasm calls start
trapping. A 62.5 KB frame there was half the reserve. Per-runtime rather than one
global keeps two runtimes validating on two threads apart, and a validation never
nests - nothing in the validator compiles or executes anything - so one buffer is
always enough.

The `/4` is empirical rather than principled: `clang.wasm` (25 MB, LLVM 8) nests
past 1000 control frames in at least one function.

Exceeding either limit is reported as `m3Err_functionStackOverflow` - including
a function declaring more locals than `d_m3ValStack` - so lowering them trades
acceptance of large or deeply-nested functions for stack footprint. It never
truncates silently.

### `d_m3MaxSane*` - `m3_core.h`

Upper bounds on declared section counts (`d_m3MaxSaneFunctionsCount`,
`d_m3MaxSaneImportsCount`, `d_m3MaxSaneUtf8Length`, and so on). These are not
spec limits; they are a cheap guard so a corrupt or hostile count field cannot
drive a huge allocation before anything else notices. Lower them on constrained
targets.

### CLI flags

| Flag | Effect |
|---|---|
| `--compile` | Disable lazy compilation: compile (and therefore validate) every function at load, for both command-line and repl loads |
| `--validate-only` | `--compile` the named file and exit - 0 if it loaded and every function compiled, 1 with the error otherwise. No function is called. Refuses to run in a build without validation support |
| `--no-validate` | Skip the validator pre-pass in every runtime the process creates, `:init` included. Contradicts `--validate-only`, which refuses the combination |
| `--spec-repl` | `--repl` plus `--compile`. What the spec test harness uses |

## Test harness

Validation is exercised by `test/run-spec-test.py` against the official
WebAssembly test suite, which it downloads on first run.

```sh
cd test
python3 run-spec-test.py                    # includes validation
python3 run-spec-test.py --no-validation    # functional assertions only
python3 run-spec-test.py --spec=wg-2.0      # the previous revision
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
with lazy compilation disabled - which is exactly what `--spec-repl` is for, and
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
  # not a gap: multiple memories lifted the one-memory limit, so the modules
  # these expect to be rejected are legal
  "* assert_invalid (multiple memories)",
```

A blacklist entry should always say whether it is a gap to be fixed or an
intentional deviation - the entry above is one wasm3 will never satisfy, because
the feature the suite predates is implemented.

Which revision an entry applies to matters. The harness accommodates **wg-3.0**
and **wg-2.0** only; the 1.1-era suites were dropped, along with the workarounds
they needed for renamed traps, the pre-2.0 lack of a bottom type, and the older
unwinding of a failed instantiation. An entry that holds for one revision and
not the other belongs in that revision's `if args.spec ==` block rather than the
top-level list.

### NaN comparison

Floating-point results are compared **by NaN class**, not by exact bits. The
spec leaves the sign of a produced NaN non-deterministic, so the harness
classifies a NaN as canonical / arithmetic / signaling from its payload and
ignores the sign. A canonical NaN satisfies an `arithmetic` expectation.

One tolerance is encoded there: Wasm3 keeps all floats in a single `f64`
register (`_fp0`, see `m3_exec_defs.h`), so an **f32 signaling NaN is quieted**
by the `float -> double -> float` round trip, even through operations that
should preserve bits. The harness accepts a quieted result where an f32
signaling NaN was expected. The reverse - a signaling NaN where the spec
requires an arithmetic one - is still a failure, and f64 stays strict.

## Known gaps

- **f32 signaling NaNs are quieted** by the shared `f64` register, as described
  above. Fixing it needs a separate f32 register or bit-exact slot storage.
- **Names containing embedded NUL bytes** are valid UTF-8 but truncated by the
  C-string representation used for function lookup. Fixing it would require
  length-prefixed name storage throughout the API.

## Adding a check

1. Decide the layer. Does it need module-wide context (`m3_parse.c`) or an
   operand-stack simulation (`m3_validate.c`)? If a rule seems to fit both,
   prefer the validator and do not duplicate it in the compiler.
2. Add the check in one place. In the parser use `_throwif`; in the validator
   return the error directly.
3. Confirm it fires. Load a module that violates the rule and check it is
   rejected - `wasm3 --spec-repl bad.wasm`.
4. Run the suite for both spec revisions, and the WASI tests. A new check that
   is too strict shows up as previously-passing functional assertions failing:

```sh
cd test
python3 run-spec-test.py
python3 run-spec-test.py --spec=wg-2.0
python3 run-wasi-test.py
```

5. If the check closes a blacklisted gap, remove that entry.

To investigate a single failure, `spec-test.log` records every assertion with
its outcome, and `--line <n>` re-runs one assertion from a `.wast`.
