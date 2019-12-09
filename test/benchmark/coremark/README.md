# CoreMark 1.0

https://github.com/eembc/coremark

### Results

```log
Node v13.0.1 (interpreter)       28     51.0x
wasm-micro-runtime               54     26.4x
wac (wax)                       105     13.6x ▲ slower
wasm3                          1428      1.0
Wasmer 0.11.0 singlepass       4285      3.0x ▼ faster
wasmtime 0.7.0 (--optimize)    4615      3.2x
Webassembly.sh (Chromium 78)   6111      4.3x
Webassembly.sh (Firefox 70)    6470      4.5x
Wasmer 0.11.0 cranelift        6875      4.8x
wasmer-js (Node v13.0.1)       9090      6.4x
Wasmer 0.11.0 llvm            10526      7.4x
WAVM                          15384     10.8x
Native (GCC 7.4.0, 32-bit)    17647     12.4x
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

make compile PORT_DIR=linux64 CC=gcc EXE=.elf
./coremark.elf
```

