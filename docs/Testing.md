# Wasm3 tests

## Running WebAssembly spec tests

To run spec tests, you need `python3`

```sh
# In test directory:
python3 ./run-spec-test.py
```

It will automatically download, extract, run the WebAssembly core test suite.

## Running WASI test

Wasm3 comes with a set of benchmarks and test programs (prebuilt as `WASI` apps) including `CoreMark`, `C-Ray`, `Brotli`, `mandelbrot`, `smallpt` and `wasm3` itself.

This test will run all of them and verify the output:

```sh
# In test directory:
python3 ./run-wasi-test.py
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

## Running coverage-guided fuzz testing with libFuzzer

You need to produce a fuzzer build first (use your version of Clang):

```sh
# In wasm3 root:
mkdir build-fuzzer
cd build-fuzzer
cmake -GNinja -DCLANG_SUFFIX="-9" ..
cmake -DBUILD_FUZZ:BOOL=TRUE ..
ninja
```

```sh
# In test directory:
../build-fuzzer/wasm3-fuzzer -detect_leaks=0 ./fuzz/corpus
```

Read [more on libFuzzer](https://llvm.org/docs/LibFuzzer.html) and it's options.

Note: to catch fuzzer errors in debugger, you need to define:
```sh
export ASAN_OPTIONS=abort_on_error=1
export UBSAN_OPTIONS=abort_on_error=1
```

