# Wasm3 tests

## Running WebAssembly spec tests

To run spec tests, you need `python3`

```sh
cd test
python3 ./run-spec-test.py
```

It will automatically download, extract, run the WebAssembly core test suite.

## Running WASI test

Wasm3 comes with a set of benchmarks and test programs (prebuilt as `WASI` apps) including `CoreMark`, `C-Ray`, `Brotli`, `mandelbrot`, `smallpt` and `wasm3` itself.

This test will run all of them and verify the output:

```sh
./run-wasi-test.py
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
