# CoreMark 1.0

https://github.com/eembc/coremark

**NOTE:** You can find results of CoreMark benchmark [here](https://github.com/wasm3/wasm3/blob/master/PERFORMANCE.md)


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

