# Performance

## CoreMark 1.0 results

```log
# Intel i5-8250U x64 (1.6-3.4GHz)

Node v13.0.1 (interpreter)       28.544923     57.0x
wac (wax)                       101.864113     16.0x
wasm-micro-runtime              201.612903      8.1x ▲ slower
wasm3                          1627.869119      1.0
Wasmer 0.13.1 singlepass       4065.591544      2.5x ▼ faster
wasmtime 0.8.0 (--optimize)    6453.505427      4.0x
Wasmer 0.13.1 cranelift        6467.164442      4.0x
Webassembly.sh (Chromium 78)   6914.325225      4.2x
Webassembly.sh (Firefox 70)    7251.153593      4.5x
wasmer-js (Node v13.0.1)      10043.827611      6.2x
Wasmer 0.13.1 llvm            12450.199203      7.6x
WAVM                          14946.566026      9.2x
Native (GCC 7.4.0, 32-bit)    18070.112035     11.1x
Native (GCC 7.4.0, 64-bit)    19144.862795     11.8x
```

**Note:** Here is more info on [how to run CoreMark benchmark](https://github.com/wasm3/wasm-coremark).

## Simple recursive Fibonacci calculation test

```log
                                            fib(40)
-----------------------------------------------------------------------------------------
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
                                            fib(24)       comments
-----------------------------------------------------------------------------------------
Maix (K210)        rv64imafc @ 400MHz          77ms
ESP8266                LX106 @ 160MHz         308ms       TCO failed,   stack used: 9024
ESP32                    LX6 @ 240MHz         297ms       TCO failed,   stack used: 10600
ESP32-s2 (beta)          LX6 @ 240MHz         297ms       TCO failed
Particle Photon       Arm M3 @ 120MHz         536ms
MXChip AZ3166         Arm M4 @ 100MHz            ms
WM W600               Arm M3 @ 80MHz          698ms       TCO enabled,  stack used: 1325
WM W600               Arm M3 @ 80MHz          826ms       TCO disabled, stack used: 8109
Arduino DUE (SAM3X8E) Arm M3 @ 84MHz          754ms
BleNano2 (nRF52)      Arm M4 @ 64MHz          1.19s
Arduino MKR1000      Arm M0+ @ 48MHz          1.93s       TCO failed
TinyBLE (nRF51)       Arm M0 @ 16MHz          5.69s       TCO failed
BluePill              Arm M3 @ 72MHz          7.62s
HiFive1 (FE310)     rv32imac @ 320MHz         9.10s    <- something wrong here?
ATmega1284               avr @ 16MHz         12.99s       TCO failed
Fomu                   rv32i @ 12MHz         25.20s
```

## Wasm3 vs other languages

```log
                                             fib(40)
-----------------------------------------------------------------------------------------
LuaJIT             jit                         1.15s
Node v10.15        jit                         2.97s ▲ faster
Wasm3              interp                      3.83s
Lua 5.1            interp                     16.65s ▼ slower
Python 2.7         interp                     34.08s
Python 3.4         interp                     35.67s
Micropython v1.11  interp                     85,00s
Espruino 2v04      interp                       >20m
```

