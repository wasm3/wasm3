## Compile

```sh
wasicc -Oz wasi_printf.c -o wasi_printf.wasm

# Disassemble:
wasm2wat wasi_printf.wasm -o wasi_printf.wat
```

## Run

```sh
../../build/wasm3 wasi_printf.wasm _start
```

