## Compile

```sh
wasicc -O3 wasi_printf.c -o wasi_printf.wasm
wasm-opt -O3 wasi_printf.wasm -o wasi_printf.wasm
wasm-strip wasi_printf.wasm
```

## Run

```sh
../../build/wasm3 wasi_printf.wasm _start

$ENGINES_PATH/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm wasi_printf.wasm

$ENGINES_PATH/wac/wax wasi_printf.wasm

$ENGINES_PATH/WAVM/Release/bin/wavm run wasi_printf.wasm

wasmer run wasi_printf.wasm

wasmer-js run wasi_printf.wasm
```

