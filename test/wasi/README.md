## Compile

```sh
wasicc -g -O0 -Wl,--stack-first test.c -o test.wasm
wasm-opt --strip-debug -Os test.wasm -o test-opt.wasm
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

