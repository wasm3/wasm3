# CoreMark 1.0

The `coremark` files in this directory were produced by:

```sh
$ make compile PORT_DIR=linux CC=emcc EXE=-side.wasm XCFLAGS="-O3 -s SIDE_MODULE=1 --llvm-lto 3"
$ make compile PORT_DIR=linux CC=emcc EXE=.html XCFLAGS="-O3 -g2 --llvm-lto 3 --closure 1"
$ make compile PORT_DIR=linux CC=wasicc EXE=-wasi.wasm XCFLAGS="-Ofast -flto"
```

### Running WASI version

```sh
export ENGINES_PATH=/opt/wasm_engines

# WAC => 101.895252
$ENGINES_PATH/wac/wax coremark-wasi.wasm

# wasm-micro-runtime => [fails]
#$ENGINES_PATH/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm coremark-wasi.wasm

# Wasmer => 7126.660188
wasmer run coremark-wasi.wasm

# Webassembly.sh
#   Chrome =>  7652.865998
#   Firefox => 7847.445656
wapm upload
coremark-wasi

# WAVM => 20273.009866
$ENGINES_PATH/wasm-jit-prototype/_build/bin/wavm run coremark-wasi.wasm
```

### Running EMCC version

```sh
# V8 (Node.js) => 10962.508222
node ./coremark.js
```

### Running native version

```sh
# Native on the same machine => 26704.052340
make compile PORT_DIR=linux64 CC=gcc EXE=.elf XCFLAGS="-O3 -flto"
./coremark.elf
```

