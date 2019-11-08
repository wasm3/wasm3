[![GitHub issues](https://img.shields.io/github/issues/vshymanskyy/wasm3.svg)](https://github.com/vshymanskyy/wasm3/issues)
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/vshymanskyy/wasm3)

# Wasm3
This is an (experimental) high performance WebAssembly interpreter written in C.

~ 10x faster than common wasm interpreters, like `wac`, `wasm-micro-runtime`, `life`  
~ 5-6x slower than fastest wasm `JIT` engines  
~ 10-15х slower that native execution  

## Status

This is **experimental**.

Currently `wasm3` runs on:
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/linux.svg" width="18" height="18" /> Linux,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/windows.svg" width="18" height="18" /> Windows,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/apple.svg" width="18" height="18" /> OS X
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/android.svg" width="18" height="18" /> Android
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/raspberrypi.svg" width="18" height="18" /> Raspberry Pi and other **SBC**s
- **MCU**s: ESP32, ESP8266, nRF52, nRF51, Blue Pill, K210, FOMU, MXChip AZ3166, Arduino Due, Arduino MKR*, etc.
- **OpenWRT**-enabled routers
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/googlechrome.svg" width="18" height="18" /> <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/mozillafirefox.svg" width="18" height="18" /> Browsers... yes, using WebAssembly itself!

Minimum system requirements: **~64Kb** for code and **~10Kb** RAM

`wasm3` is built on top of **Steven Massey**'s novel interpreter topology, with
- Aim at Wasm 1.0 spec conformance (not there yet)
- Lot's of fixes
- Portability improvements
