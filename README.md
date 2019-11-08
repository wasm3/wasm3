[![GitHub issues](https://img.shields.io/github/issues/vshymanskyy/wasm3.svg)](https://github.com/vshymanskyy/wasm3/issues)
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/vshymanskyy/wasm3)

# Wasm3
This is an (experimental) high performance WebAssembly interpreter written in C.

## Status
Currently `wasm3` runs on:
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/linux.svg" width="18" height="18" /> Linux,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/windows.svg" width="18" height="18" /> Windows,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/apple.svg" width="18" height="18" /> OS X
- SBCs (Raspberry Pi and the likes)
- Microcontrollers: ESP32, ESP8266, nRF52, nRF51, Blue Pill, Kendryte 210, FOMU, MXChip AZ3166, Arduino Due,
Arduino MKR*, etc.  
- Routers (via OpenWRT)
- Browsers... yes, using WebAssembly itself!

Minimal system requirements are **~64Kb** for code and **~10Kb** RAM.

Wasm3 is built on top of **Steven Massey**'s novel interpreter topology, with
- Aim at Wasm 1.0 spec conformance (not there yet)
- Lot's of fixes
- Portability improvements

## Performance

`TODO`
