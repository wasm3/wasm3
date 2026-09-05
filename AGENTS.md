# Working on Wasm3

Wasm3 is a WebAssembly interpreter written in C99, plus a CLI, a set of embedded ports,
and the test suites that keep it honest.

| Path | What lives there |
|---|---|
| `source/` | the engine and the public API (`wasm3.h`) |
| `source/extensions/` | opt-in API that is deliberately not part of `libm3` |
| `platforms/app/` | the `wasm3` CLI and its REPL |
| `platforms/` | the embedded, language and OS ports |
| `test/` | the suites and their Python runners |
| `extra/` | tooling: the source formatter, the pre-push check |
| `docs/` | reference documentation |

## Before you push

```sh
python extra/check.py
```

Spelling and format checks, then a `-Werror` build, then the embedding API tests,
regression cases, spec suite (wg-3.0 and wg-2.0) and WASI apps - the same steps in the
same order as CI, against one local build. Around six minutes. `--list` shows the
stages; naming them runs a subset.

Green here is the best single-machine predictor of a green matrix, but it is not proof:
CI runs that sequence across 70+ configurations, and some failures exist only in
one of them. Do not use CI as the experiment loop - a red matrix run costs a push cycle.

## Build

[docs/Development.md](docs/Development.md) has the prerequisites, and covers Windows,
cross-compilation, the WASI SDK, Zig and the microcontroller targets. The build this
repository's own checks assume is:

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_WERROR=ON
cmake --build build
```

`BUILD_TESTS` adds `m3_test` and `m3_test_reftypes`, the only coverage of the embedding
API; it is off by default and on in CI. `BUILD_WERROR` applies to Wasm3's own targets
only - libuv and uvwasi are fetched and built here too, and their warnings are not ours
to fix.

On Windows, no `cmake`, `ninja` or compiler on `PATH` does not mean there is no
toolchain - it is commonly in WSL instead. Look there before reporting the build as
unavailable:

```sh
wsl -e bash -lc 'command -v cmake ninja gcc clang python3'
```

If that answers, build and test in WSL: the Linux instructions apply as written, and
[Driving build from a Windows shell](docs/Development.md#driving-build-from-a-windows-shell)
covers the argument quoting and path rewriting that otherwise mangle commands sent across
from the Windows side.

Worth knowing: `-DBUILD_WASI=none|simple|uvwasi|metawasi` selects the WASI
implementation (`uvwasi` by default, `simple` when a toolchain has no `stdatomic.h`),
and a sanitizer build is `-DCMAKE_BUILD_TYPE=Debug -DBUILD_WASI=simple` with
`-fsanitize=address,undefined` in `CFLAGS` and `LDFLAGS`.

## Test

[docs/Testing.md](docs/Testing.md) covers every runner and how to narrow one down to a
single suite, file or assertion. Two of its rules are worth carrying in your head,
because breaking either produces a Python traceback about the harness that reads like a
broken engine:

- `--exec` defaults to `../build/wasm3`, so name the binary you actually built.
- The spec runner's `--exec` must also carry `--spec-repl`.

`extra/check.py` gets both right on its own.

A regression case is committed as text alone - `.wat`, or `.wast` where it has to name
its own bytes - and `run-regression-test.py` assembles it into a temporary directory on
every run. Nothing has to be assembled by hand, and no `.wasm` belongs in a commit.

## House rules

**The build is warning-free.** Fix every warning it emits, including ones in code your
change did not touch. The bar is the state of the tree, not the delta against `main`; a
baseline diff is useful for *locating* what a change added, never for deciding what to
leave behind.

The default cmake build hides plenty of them, so a warning sweep is finished when this
matrix is clean, not when the build is:

```sh
cd source
for cfg in '' '-Dd_m3HasTypedRefs=1' '-DDEBUG=1'; do
  for pic in '' '-fPIC'; do
    for f in *.c; do
      gcc -c $cfg $pic -O3 -Wall -Wextra -Wtype-limits -Wjump-misses-init \
          -Wno-unused-parameter -I. -o /tmp/w.o $f || echo "FAIL $f [$cfg $pic]"
    done
  done
done
```

Each axis hides something specific. `-fPIC`, which cmake adds, changes GCC's inlining
enough to suppress most `-Wmaybe-uninitialized` in `m3_parse.c`. `d_m3HasTypedRefs`
gates whole branches, such as `ParseValueType`'s ref/refNull case. `DEBUG` gates
`m3_info.c`, where several locals exist only to feed `m3log` and compile away otherwise.
And `-Wjump-misses-init` fires on an initialized declaration placed after a `_throwif`
or `_()` while still in scope at `_catch:` - declare it inside a nested block instead.

**Format with `extra/format.py`, never with bare `clang-format`.** Three Wasm3
conventions cannot be expressed in a clang-format config at all and are restored by a
post-pass; running clang-format on its own destroys them. The header of
[.clang-format](.clang-format) names them and says why, `format.py`'s docstring says how,
and [docs/Development.md](docs/Development.md#prerequisites) has the pinned versions.

After changing `format.py` or `.clang-format`, judge the result by more than the diff
size: idempotency (`format_text(format_text(x)) == format_text(x)` for every file),
identical `gcc -E -P` output ignoring whitespace and `__FILE__`/`__LINE__`, and no new
`-Wall -Wextra` diagnostic.

**Reach for the WABT tools the tree ships, not a system `wabt` or a web assembler.**
Whenever something has to be turned into a module, read back out of one, or expanded into
a spec script - writing a test case, minimising a reported module, checking what an
encoding actually says - use `test/wasi/wabt/`, run under Wasm3 itself:

```sh
build/wasm3 test/wasi/wabt/wat2wasm.wasm --enable-all in.wat -o out.wasm
build/wasm3 test/wasi/wabt/wasm2wat.wasm --enable-all in.wasm
build/wasm3 test/wasi/wabt/wast2json.wasm --enable-all in.wast -o out.json
build/wasm3 test/wasi/wabt/wasm-objdump.wasm -d in.wasm   # takes no --enable-all
```

`wast2json` writes the modules beside its `.json`, and `wasm2wat` prints to stdout unless
given `-o`. Pass `--enable-all` to the other three by default: without the flag for a
feature they answer `error: opcode not allowed` on anything post-MVP. `wasm-objdump` has
no feature flags at all and rejects the option.

Going through the tree pins the toolchain to what it carries, so a module does not
silently depend on whichever `wabt` a machine happens to have, and it exercises the
engine on a real program every time. They are WASI programs, so a `-DBUILD_WASI=none`
build cannot drive them - use one that can, and pass `run-regression-test.py --host` when
the build under test is that one, or is slow.

Add `--stack-size 1048576` when a `[trap] stack overflow` comes out of the tool rather
than out of what it was given. WABT is a real program and the CLI's 64 KB default is
sized for test modules: a deeply nested module needs most of it, and a build that spends
slots faster - a 32-bit target with `-Dd_m3Use32BitSlots=0` - runs out. The runners
already pass it.

`wast2json` over `wat2wasm` when the bytes themselves matter: it writes `(module binary
...)` out untouched, and wrapped in `assert_malformed` it will even write a module that
does not decode, where `wat2wasm` re-encodes both into its own canonical form.

`wasm2wat` is also the second opinion on whether a module is valid, so there is no
separate validator here to reach for. It checks by default, and it decides the same
question `wasm3 --validate-only` does while saying far more about the answer - `error:
type mismatch in i32.add, expected [i32, i32] but got [f32]` where Wasm3 says `incorrect
type on stack`. Then `--no-check` prints the module anyway, which is what you want next.

`wasm-objdump -d` is the only thing here that puts a byte offset on each instruction, so
it is what turns an offset back into code - a `test/strace/*.txt` backtrace, a Wasm3
error, a fuzz corpus entry. `-x` shows the section details, `-s` the raw bytes. Reach for
`wasm2wat -v` instead when the section walk is enough and the offsets are not.

**A CLI flag is not a reason to grow the public API.** Wire new `wasm3` behaviour out of
what `platforms/app/main.c` already has - `repl_init`, `repl_load`, `repl_compile`, the
`argCompile` switch - before considering a new `m3_*` entry point in `wasm3.h`. Every
public function there is a permanent commitment across every embedder and platform
config. Reuse also costs something (going through `repl_load` instantiates and runs the
start function, so an unsatisfied import fails a load a validator would have passed);
say what the trade is rather than silently picking the purer implementation. A genuinely
new capability that no existing path exposes is still fair game.

**A validation check lives in exactly one layer**, and the compiler is not one of them.
[docs/Validation.md](docs/Validation.md) sets out which layer owns what, what each knob
costs, and how to add a check - it is written as a rule, not a description, so read it
before touching either layer.

## Reading and editing the C

Roughly 26k lines and 600+ macros, in a house style that a short window of context will
mislead you about. Know these before editing.

### The exception macros

`source/m3_exception.h` emulates try/catch with `goto`. A function that uses it opens
with `_try` - which *declares* `M3Result result` - calls fallible things through `_(...)`,
and must end with a reachable `_catch:` label:

```c
static
M3Result CopyStackTopToRegister (IM3Compilation o, bool i_updateStack)
{
    M3Result result = m3Err_none;

_       (PreserveRegisterIfOccupied(o, type));
_       (EmitOp(o, op));

    _catch: return result;
}
```

`_` is a one-character macro name, and the formatter hoists it to column 0 so that all
exception plumbing shares one column - `_catch:` included. `_throw(err)` and
`_throwif(err, cond)` jump to the same label. `m3_compile.c` alone has hundreds of these.

### What the formatter does that clang-format cannot

Three things, and each is a reason a line looks the way it does: the `_(...)` hoist above;
`d_m3Assert` and `m3log` calls kept in a right-hand sidebar column the author chose; and
lines held out of the run and restored byte for byte - `#define` bodies, the
`_try {` / `} _catch:` pair, and sidebar entries whose call wraps. Hand-aligned tables
whose columns carry meaning - the opcode and fold tables in `m3_compile.c`, the op tables
in `m3_exec.h`, the three config headers - are fenced with `// clang-format off` / `on`
rather than approximated. Preserve the alignment when you add a row.
