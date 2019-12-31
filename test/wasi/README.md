## Compile

```sh
wasicc -O3 test.c -o test.wasm
wasm-opt -O3 test.wasm -o test-opt.wasm
wasm-strip test-opt.wasm
```

```sh
wasicc -O3 test_native_vs_raw.c -o test_native_vs_raw.wasm -Wl,--allow-undefined-file=wasm_api.syms
wasm-opt -O3 test_native_vs_raw.wasm -o test_native_vs_raw.wasm
wasm-strip test_native_vs_raw.wasm
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

