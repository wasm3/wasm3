# Performance

```log
Function:                       fib(40)
-----------------------------------------------------
 Device:    Lenovo Ideapad 720s [i5-8250U @ 1.60GHz]
-----------------------------------------------------
Linux       x64   gcc 7.4.0       4.63s
Linux       x64   clang 9         5.32s
Win 10      x64   clang 9         5.35s
Win 10      x64   msvc 2019       6.10s
Win 10      x86   clang           9.43s  - no TCO
Linux       x86   gcc            11.34s
Linux       x86   clang          15.37s  - no TCO
```

## Running

```sh
# WAC
time $(ENGINES_PATH)/wac/wac fib32.wasm fib 40

# WAMR
time $(ENGINES_PATH)/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm -f fib fib32.wasm 40

# Wasmer
time wasmer run --em-entrypoint fib fib32.wasm -- 40

# WAVM
time $(ENGINES_PATH)/wasm-jit-prototype/_build/bin/wavm run -f fib fib32.wasm 40
```
