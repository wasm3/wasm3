# CoreMark 1.0

The `coremark` files in this directory were produced by:

```sh
$ make compile PORT_DIR=barebones CC=wasicc EXE=-wasi.wasm
$ make compile PORT_DIR=barebones CC=emcc   EXE=-emcc.wasm
$ make compile PORT_DIR=barebones CC=emcc   EXE=.html
```

**Note:** do not forget to update your SDK
```sh
emsdk install latest # or latest-fastcomp
emsdk activate latest
```

### Running WASI version in Wasm3

```sh
../../../build/wasm3 coremark-wasi.wasm _start
```

### Running WASI version on other engines

```sh
export ENGINES_PATH=/opt/wasm_engines

# WAC => 158.215331
$ENGINES_PATH/wac/wax coremark-wasi.wasm

# wasm-micro-runtime => 51.813472
$ENGINES_PATH/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm coremark-wasi.wasm

# Wasmer => 7026.509103
wasmer run coremark-wasi.wasm

# Webassembly.sh
#   Chrome =>  7472.724555
#   Firefox => 7945.967422
wapm upload
coremark-wasi

# Wasmer-JS (V8) => 10024.057739
# https://www.npmjs.com/package/@wasmer/cli
wasmer-js run coremark-wasi.wasm


# WAVM => 14781.966001
$ENGINES_PATH/wasm-jit-prototype/_build/bin/wavm run coremark-wasi.wasm
```

### Running EMCC version

```sh
# V8 (Node.js) => 10962.508222
node ./coremark.js
```

### Running native version

```sh
# Native on the same machine => 17849.705480
make compile PORT_DIR=linux CC=gcc EXE=.elf XCFLAGS="-m32"
./coremark.elf

# Native on the same machine => 20202.020202
make compile PORT_DIR=linux64 CC=gcc EXE=.elf
./coremark.elf
```

