# Brotli 1.0.7

https://github.com/google/brotli

### Results

```log
TODO
```

### Building

```sh
export CC=wasicc
make
cd bin
wasm-opt -O3 brotli.wasm -o brotli.wasm
```

### Running

```sh
export ENGINES_PATH=/opt/wasm_engines

# Wasm3
cat alice29.txt | ../../../build/wasm3 brotli.wasm -c > alice29.txt.comp

# WAC
cat alice29.txt | $ENGINES_PATH/wac/wax brotli.wasm -c > alice29.txt.comp

# wasm-micro-runtime
cat alice29.txt | $ENGINES_PATH/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm brotli.wasm -c > alice29.txt.comp

# wasmtime
cat alice29.txt | wasmtime --optimize brotli.wasm -- -c > alice29.txt.comp

# Wasmer
cat alice29.txt | wasmer run brotli.wasm -- -c > alice29.txt.comp

# Wasmer-JS (V8)
cat alice29.txt | wasmer-js run brotli.wasm -- -c > alice29.txt.comp

cat alice29.txt | node --wasm_interpret_all $(which wasmer-js) run brotli.wasm -- -c > alice29.txt.comp

# WAVM
cat alice29.txt | $ENGINES_PATH/WAVM/Release/bin/wavm run brotli.wasm -c > alice29.txt.comp
```
