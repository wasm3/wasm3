## Compile

```sh
wasicc -O3 test.c -o test.wasm
wasm-opt -O3 test.wasm -o test-opt.wasm
wasm-strip test-opt.wasm
```

## Run

```sh
../../build/wasm3 test.wasm _start

$ENGINES_PATH/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm test.wasm

$ENGINES_PATH/wac/wax test.wasm

$ENGINES_PATH/WAVM/Release/bin/wavm run test.wasm

wasmer run test.wasm

wasmer-js run test.wasm
```

