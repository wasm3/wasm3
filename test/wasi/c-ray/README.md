# C-Ray 1.1

<p align="center"><img width="30%" src="sphfract.jpg"> <img width="30%" src="scene.jpg"></p>

### Results

```log
                           time(ms)
Node v13.0.1 (interpreter)   181527
wasm-micro-runtime            78499
wac (wax)                         -
wasm3                             -
Wasmer 0.11.0 singlepass       1447
wasmtime 0.7.0 (--optimize)     576
Wasmer 0.11.0 cranelift         565
wasmer-js (Node v13.0.1)        336
Wasmer 0.11.0 llvm            crash
WAVM                            299
Native (GCC 7.4.0, 32-bit)      249
```

### Building

```sh
wasicc -g0 -O3 c-ray-f.c -Dunix -o c-ray.wasm
```

### Running

```sh
export ENGINES_PATH=/opt/wasm_engines

# Wasm3
cat scene | ../../../build/wasm3 c-ray.wasm -s 1024x768 > foo.ppm

# wasm-micro-runtime
cat scene | $ENGINES_PATH/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm c-ray.wasm -s 1024x768 > foo.ppm

# wasmtime
cat scene | wasmtime --optimize c-ray.wasm -- -s 1024x768 > foo.ppm

# Wasmer
cat scene | wasmer run c-ray.wasm -- -s 1024x768 > foo.ppm
cat scene | wasmer run --backend singlepass c-ray.wasm -- -s 1024x768 > foo.ppm
cat scene | wasmer run --backend llvm       c-ray.wasm -- -s 1024x768 > foo.ppm

# Wasmer-JS (V8)
cat scene | wasmer-js run c-ray.wasm -s 1024x768 > foo.ppm

cat scene | node --wasm_interpret_all $(which wasmer-js) run c-ray.wasm -s 1024x768 > foo.ppm

# WAVM
cat scene | $ENGINES_PATH/WAVM/Release/bin/wavm run c-ray.wasm -s 1024x768 > foo.ppm
```
