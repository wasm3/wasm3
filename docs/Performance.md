# Performance

## CoreMark 1.0 results

```log
Node v13.0.1 (interpreter)       28     51.0x
wac (wax)                       105     13.6x
wasm-micro-runtime              157      9.1x ▲ slower
wasm3                          1428      1.0
Wasmer 0.11.0 singlepass       4285      3.0x ▼ faster
wasmtime 0.7.0 (--optimize)    4615      3.2x
Webassembly.sh (Chromium 78)   6111      4.3x
Webassembly.sh (Firefox 70)    6470      4.5x
Wasmer 0.11.0 cranelift        6875      4.8x
wasmer-js (Node v13.0.1)       9090      6.3x
Wasmer 0.11.0 llvm            10526      7.4x
WAVM                          15384     10.8x
Native (GCC 7.4.0, 32-bit)    17976     12.6x
Native (GCC 7.4.0, 64-bit)    19104     13.4x
```

## Simple recursive Fibonacci calculation test

```log
                                            fib(40)
----------------------------------------------------------------------------------------------------
# Intel i5-8250U x64 (1.6-3.4GHz)
Native C implementation                       0.23s
Linux                                         3.83s
Win 10                                        5.35s
wasm3 on V8 (Emscripten 1.38, node v13.0.1)  17.98s

# Raspberry Pi 4 BCM2711B0 armv7l (A72 @ 1.5GHz)
Native C implementation                       1.11s
Linux                                        22.97s

# Orange Pi Zero Plus2 H5 aarch64 (A53 @ 1GHz)
Native C implementation                       2.55s
Linux                                        50.00s

# VoCore2 mips32r2 (MT7628AN @ 580MHz)
Native C implementation                       6.21s
OpenWRT                                      2m 38s

# Xiaomi Mi Router 3G mips32r2 (MT7621AT @ 880MHz)
Native C implementation                       8.83s
OpenWRT                                      3m 20s
```

## Wasm3 on MCUs

```log
                                          fib32(24)   fib64(24)      comments
----------------------------------------------------------------------------------------------------
Maix (K210)        rv64imafc @ 400MHz          77ms        77ms
ESP8266                LX106 @ 160MHz         308ms       321ms      TCO failed,   stack used: 9024
ESP32                    LX6 @ 240MHz         340ms       350ms      TCO failed,   stack used: 10600
ESP32-s2 (beta)          LX6 @ 240MHz         340ms       351ms      TCO failed
Particle Photon       Arm M3 @ 120MHz         536ms       562ms
MXChip AZ3166         Arm M4 @ 100MHz            ms          ms
WM W600               Arm M3 @ 80MHz          698ms       782ms      TCO enabled,  stack used: 1325
WM W600               Arm M3 @ 80MHz          826ms       914ms      TCO disabled, stack used: 8109
Arduino DUE (SAM3X8E) Arm M3 @ 84MHz          754ms       813ms
BleNano2 (nRF52)      Arm M4 @ 64MHz          1.19s       1.29s
Arduino MKR1000      Arm M0+ @ 48MHz          1.93s       2.06s      TCO failed
TinyBLE (nRF51)       Arm M0 @ 16MHz          5.69s       5.93s      TCO failed
BluePill              Arm M3 @ 72MHz          7.62s       8.20s
HiFive1 (FE310)     rv32imac @ 320MHz         9.10s       9.82s   <- something wrong here?
ATmega1284               avr @ 16MHz         12.99s        ---       TCO failed
Fomu                   rv32i @ 12MHz         25.20s      26.10s
```


## Wasm3 vs other WebAssembly engines

This is how different engines run the same function on Intel i5-8250U (1.6-3.4GHz):

```log
                                             fib(40)
----------------------------------------------------------------------------------------------------
WAVM               jit                         0.62s
wasmer             jit                         0.70s
V8 (Node.js)       jit                         0.74s
SpiderMonkey       jit                         0.93s ▲ faster
Wasm3              interp                      3.83s
iwasm              interp                     25.70s ▼ slower
wac                interp                     37.11s
```

## Wasm3 vs other languages

```log
                                             fib(40)
----------------------------------------------------------------------------------------------------
LuaJIT             jit                         1.15s
Node v10.15        jit                         2.97s ▲ faster
Wasm3              interp                      3.83s
Lua 5.1            interp                     16.65s ▼ slower
Python 2.7         interp                     34.08s
Python 3.4         interp                     35.67s
Micropython v1.11  interp                     85,00s
Espruino 2v04      interp                       >20m
```

