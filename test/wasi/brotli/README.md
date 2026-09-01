# Brotli 1.2.0

https://github.com/google/brotli

### Results

```log
TODO
```

### Building

With [WASI SDK](https://github.com/WebAssembly/wasi-sdk) 34, from a checkout of the tag
above. Two things wasi-libc does not hand over as-is: `chown` is stubbed out the same way
Brotli itself stubs it on Windows, since it is only reached when writing a file, which
this build never does; and `clock`, which Brotli calls to time itself, needs the wall
clock emulation, WASI having no process-associated clock of its own.

```sh
git clone -b v1.2.0 --depth 1 https://github.com/google/brotli
cd brotli

$WASI_SDK_PATH/bin/clang -mcpu=lime1 -g0 -O3 -Wl,--strip-all \
    '-Dchown(F,O,G)=0' -D_WASI_EMULATED_PROCESS_CLOCKS \
    -Ic/include c/common/*.c c/dec/*.c c/enc/*.c c/tools/brotli.c \
    -lwasi-emulated-process-clocks -o brotli.wasm
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
