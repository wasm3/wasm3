# Wasm3 tests

Everything here needs Python 3 and a built `wasm3`. See
[Development](./Development.md) for the prerequisites and the build.

Every runner takes `--exec` and defaults it to `../build/wasm3`, so point it at the
binary you actually built whenever the build tree is not `build/`. The spec runner is
the one exception to a plain path: it drives the interpreter as a REPL, so its `--exec`
has to carry `--spec-repl` as well.

## Running everything

```sh
python extra/check.py
```

Formatting and spelling, a `-Werror` build, then all four suites below in CI's own
order. `--list` shows the stages, and naming them runs a subset. It picks the binary out
of the build tree itself, so nothing here needs `--exec`.

## Running WebAssembly spec tests

```sh
# In test directory:
python3 run-spec-test.py
```

It will automatically download, extract, run the WebAssembly core test suite. The
current revision is `wg-3.0`; `--spec=wg-2.0` runs the previous one, and CI runs both.

This includes checking that invalid modules are rejected. See
[Validation](./Validation.md) for how those checks work and how to skip them.

`--skip-features` names the features the build is expected *not* to have -
`tail-call`, `typed-refs`, `multi-memory`. The default `auto` reads them off the version
banner, which means a build that quietly loses one also drops the tests covering it and
still reports success; naming them instead makes that case fail. CI names them.

To narrow a run down: supply path to a `.json` file to run one suite, `--line <n>` to re-run a
single assertion, and read `spec-test.log` for every assertion and its outcome.

## Running WASI test

Wasm3 comes with a set of benchmarks and test programs (prebuilt as `WASI` apps) including `CoreMark`, `C-Ray`, `Brotli`, `mandelbrot`, `smallpt` and `wasm3` itself.

This test will run all of them and verify the output:

```sh
# In test directory:
python3 run-wasi-test.py
```

It can be run against other engines as well:

```sh
./run-wasi-test.py --exec wasmtime                    # [PASS]
./run-wasi-test.py --exec "wavm run"                  # [PASS]
./run-wasi-test.py --exec "wasmer run"                # [PASS]
./run-wasi-test.py --exec "wasmer-js run"             # [PASS]
./run-wasi-test.py --exec $WAMR/iwasm --timeout=300   # [PASS, but very slow]
./run-wasi-test.py --exec $WAC/wax   --timeout=300    # [FAIL, crashes on most tests]
```

## Running regression tests

```sh
# In test directory:
python3 run-regression-test.py
```

The cases in `test/regression/` are modules that once broke Wasm3, kept as text so that a
diff stays readable: `.wat` for a plain module, `.wast` where the case needs the script
grammar to name its own bytes. Add one whenever you fix a bug that a spec test would not
have caught.

Nothing has to be assembled by hand: the runner does it before the run, with the
`wast2json` in `test/wasi/wabt/`, which is itself a module and runs on the interpreter
under test. Pass `--host ../build/wasm3` if the interpreter
under test is not capable of running WABT.

## Running strace tests

```sh
# In test directory:
python3 run-strace-test.py --exec ../build-strace/wasm3
```

Each case runs a module and compares the trace Wasm3 writes to stderr against a recorded
reference; `--update` re-records them. A reference carries code offsets, so re-record it
after changing a case's source or the vendored `wast2json` that assembles it. This needs
its own build, because tracing changes how the engine runs:

```sh
cmake -S . -B build-strace -DCMAKE_C_FLAGS="-DDEBUG -Dd_m3EnableStrace=2 -Dd_m3RecordBacktraces=1"
cmake --build build-strace
```

## Benchmarking

```sh
# In test directory:
./run-bench.py --exec "old=../build-old/wasm3" --exec "new=../build/wasm3" --rounds 5
```

Compares command lines over the WASI workloads. Each `--exec` is `<name>=<command>`,
where the command is however the engine under test is invoked - a binary, that binary
plus runtime flags, or another engine entirely - and the name is what labels the column.
The harness builds nothing, so what it compares is exactly what it was handed:

```sh
# what a runtime flag costs, one binary
./run-bench.py --exec "plain=../build/wasm3" \
               --exec "metered=../build/wasm3 --gas-limit 1e12"

# what a build option costs, two binaries
cmake -S .. -B ../build-preload -DCMAKE_C_FLAGS=-Dd_m3PreloadNextOp=1
cmake --build ../build-preload
./run-bench.py --exec "base=../build/wasm3" --exec "preload=../build-preload/wasm3"
```

Rounds run interleaved and rotated, so a machine that drifts during the session
penalises every command equally. `--filter` selects workloads by glob, `--cpu` pins runs
with `taskset`, `--json` writes the raw per-round samples, and `--list` prints the
workloads and the metrics each one reports.

## Running coverage-guided fuzz testing with libFuzzer

You need to produce a fuzzer build first. It requires Clang, and `BUILD_FUZZ` is only
honoured when the build was told to use it:

```sh
# In wasm3 root:
cmake -S . -B build-fuzzer -GNinja -DCLANG=1 -DBUILD_FUZZ=ON
cmake --build build-fuzzer
```

`CLANG_SUFFIX` picks a specific version - `-DCLANG_SUFFIX="-18"` for `clang-18` - when
several are installed side by side.

```sh
# In test directory:
../build-fuzzer/wasm3-fuzzer -detect_leaks=0 ./fuzz/corpus
```

Read [more on libFuzzer](https://llvm.org/docs/LibFuzzer.html) and its options.

Note: to catch fuzzer errors in debugger, you need to define:
```sh
export ASAN_OPTIONS=abort_on_error=1
export UBSAN_OPTIONS=abort_on_error=1
```

Wasm3 is also fuzzed continuously by [OSS-Fuzz](https://github.com/google/oss-fuzz), and
every push here runs a 300-second CIFuzz session against that project - see
`.github/workflows/cifuzz.yml`. The local fuzzer's entry point is
`platforms/app_fuzz/fuzzer.c`.
