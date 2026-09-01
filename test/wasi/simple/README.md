## Compile

With [WASI SDK](https://github.com/WebAssembly/wasi-sdk) 34. `--strip-debug` drops the
DWARF but keeps the name section, so wasm3 backtraces still name the functions - which
is what `run-strace-test.py` checks this module for.

```sh
$WASI_SDK_PATH/bin/clang -mcpu=lime1 -O0 -Wl,--stack-first -Wl,--strip-debug test.c -o test.wasm
wasm-opt --strip-debug -O3 test.wasm -o test-opt.wasm
```

## Run

```sh
../../build/wasm3 test.wasm

$ENGINES_PATH/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm test.wasm

$ENGINES_PATH/wac/wax test.wasm

$ENGINES_PATH/WAVM/Release/bin/wavm run test.wasm

wasmer run test.wasm

wasmer-js run test.wasm
```

