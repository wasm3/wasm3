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
Chrome     wasm   emcc 1.38      30.42s  --experimental-wasm-return-call --wasm-opt --wasm-no-bounds-checks --wasm-no-stack-checks

--- other wasm engines
WAVM        x64   0.8.0           0,62s
wasmer      x64   0.8.0           0.70s
V8          x64   Node.js         0.74s
V8          x64   Chrome          0.74s
SpMonkey    x64   JS Shell        0.93s
iwasm       x64                  25.70s  - interp
wac         x64                  37.11s  - interp

--- other languages
C           x64   gcc             0.23s  - native
Lua         x64   LuaJIT          1.15s  - jit
JS          x64   Node v10.15     2.97s  - jit
Lua         x64   Lua 5.1        16.65s
Python      x64   2.7            34.08s
Python      x64   3.4            35.67s
Micropython x64   v1.11          85,00s
Espruino    x64   2v04             >20m
```

```log
-----------------------------------------------------
 Device:    Raspberry Pi 4 [BCM2711B0 A72 @ 1.5GHz]
-----------------------------------------------------
Linux      armv7l gcc 8.3        23.78s
```

## Running

```sh
# WAC
time $(ENGINES_PATH)/wac/wac fib.wasm _fib 40

# WAMR
time $(ENGINES_PATH)/wasm-micro-runtime/core/iwasm/products/linux/build/iwasm -f _fib fib.wasm 40

# Wasmer
time wasmer run --em-entrypoint _fib fib.wasm -- 40

# WAVM
time $(ENGINES_PATH)/wasm-jit-prototype/_build/bin/wavm run -f _fib fib.wasm 40
```
