# CoreMark 1.0

https://github.com/eembc/coremark

### Results

```log
Node v13.0.1 (interpreter)       28     59.5x
wasm-micro-runtime               54     30.8x
wac (wax)                       105     15.8x ▲ slower
wasm3                          1666      1.0
Wasmer 0.11.0 singlepass       4285      2.6x ▼ faster
wasmtime 0.7.0 (--optimize)    4615      2.8x
Webassembly.sh (Chromium 78)   6111      3.7x
Webassembly.sh (Firefox 70)    6470      3.9x
Wasmer 0.11.0 cranelift        6875      4.1x
wasmer-js (Node v13.0.1)       9090      5.4x
Wasmer 0.11.0 llvm            10526      6.3x
WAVM                          15384      9.2x
Native (GCC 7.4.0, 32-bit)    19104     11.5x
```

### Building

The `coremark` files in this directory are produced by:

```sh
source /opt/emsdk/emsdk_env.sh --build=Release

make compile PORT_DIR=linux CC=wasicc EXE=-wasi-nofp.wasm XCFLAGS="-DHAS_FLOAT=0"
make compile PORT_DIR=linux CC=wasicc EXE=-wasi.wasm
make compile PORT_DIR=linux CC=emcc   EXE=.html
```

**Note:** do not forget to update your SDK
```sh
emsdk install latest # or latest-fastcomp
emsdk activate latest
```

### Running

```sh
export ENGINES_PATH=/opt/wasm_engines

# Wasm3
../../../build/wasm3 coremark-wasi.wasm

# WAC
$ENGINES_PATH/wac/wax coremark-wasi.wasm

# wasm-micro-runtime
$ENGINES_PATH/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm coremark-wasi.wasm

# wasmtime
wasmtime --optimize coremark-wasi.wasm

# Wasmer
wasmer run coremark-wasi.wasm

# Webassembly.sh
wapm upload
coremark-wasi

# Wasmer-JS (V8)
wasmer-js run coremark-wasi.wasm


# WAVM
$ENGINES_PATH/WAVM/Release/bin/wavm run coremark-wasi.wasm
```

### Running EMCC version

```sh
# V8 (Node.js) => 10962.508222
node ./coremark.js
```

### Running native version

```sh
# Native on the same machine
make compile PORT_DIR=linux CC=gcc EXE=-x86.elf XCFLAGS="-m32"
./coremark-x86.elf

make compile PORT_DIR=linux64 CC=gcc EXE=-x64.elf
./coremark-x64.elf
```

