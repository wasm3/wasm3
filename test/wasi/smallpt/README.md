# smallpt

Based on https://www.kevinbeason.com/smallpt/

<p align="center"><img width="50%" src="image.jpg"></p>

### Results

```log
TODO
```

### Building

```sh
wasic++ -g0 -Oz -Wl,--stack-first -Wl,-z,stack-size=$[1024*1024] smallpt-ex.cpp -o smallpt-ex.wasm
```

### Running

```sh
export ENGINES_PATH=/opt/wasm_engines

# Wasm3
../../../build/wasm3 smallpt-ex.wasm > image.ppm

# WAC
$ENGINES_PATH/wac/wax smallpt-ex.wasm > image.ppm

# wasm-micro-runtime
$ENGINES_PATH/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm smallpt-ex.wasm > image.ppm

# wasmtime
wasmtime --optimize smallpt-ex.wasm > image.ppm

# Wasmer
wasmer run smallpt-ex.wasm > image.ppm
wasmer run --backend singlepass smallpt-ex.wasm > image.ppm
wasmer run --backend llvm       smallpt-ex.wasm > image.ppm

# Wasmer-JS (V8)
wasmer-js run smallpt-ex.wasm > image.ppm

node --wasm_interpret_all $(which wasmer-js) run smallpt-ex.wasm > image.ppm

# WAVM
$ENGINES_PATH/WAVM/Release/bin/wavm run smallpt-ex.wasm > image.ppm
```
