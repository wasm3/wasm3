<img align="right" width="30%" src="/extra/screenshot.png">

[![GitHub issues](https://img.shields.io/github/issues/vshymanskyy/wasm3.svg)](https://github.com/vshymanskyy/wasm3/issues)
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/vshymanskyy/wasm3)

# <img src="/extra/wasm-symbol.svg" width="26" height="26" /> Wasm3
This is an (experimental) high performance WebAssembly interpreter written in C.

**∼ 10x faster** than other wasm interpreters (`wasm-micro-runtime`, `wac`, `life`)  
**∼ 5-6x slower** than state of the art wasm `JIT` engines, like `liftoff`  
**∼ 10-15х slower** than native execution  
<sub>* Your mileage may vary</sub>

## Status

Minimum useful system requirements: **~64Kb** for code and **~10Kb** RAM

`wasm3` currently runs on a wide range of [platforms](/platforms):
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/linux.svg" width="18" height="18" /> Linux,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/windows.svg" width="18" height="18" /> Windows,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/apple.svg" width="18" height="18" /> OS X
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/android.svg" width="18" height="18" /> Android
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/raspberrypi.svg" width="18" height="18" /> Raspberry Pi, Orange Pi and other **SBC**s
- <img src="https://cdn.rawgit.com/feathericons/feather/master/icons/cpu.svg" width="18" height="18" /> **MCU**s:  
 Arduino MKR*, Arduino Due,  
 ESP8266, ESP32, Air602 (W600), nRF52, nRF51,  
 Blue Pill (STM32F103C8T6), MXChip AZ3166 (EMW3166),  
 Maix (K210), HiFive1 (E310), Fomu (ICE40UP5K), etc.
- <img src="https://cdn.rawgit.com/feathericons/feather/master/icons/wifi.svg" width="18" height="18" /> **OpenWRT**-enabled routers
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/mozillafirefox.svg" width="18" height="18" /> <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/googlechrome.svg" width="18" height="18" /> <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/safari.svg" width="18" height="18" /> <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/microsoftedge.svg" width="18" height="18" /> Browsers... yes, using WebAssembly itself!
- <img src="extra/wasm-symbol.svg" width="18" height="18" /> `TODO:` run on `wasm3` (should be self-hosting)

`wasm3` is built on top of [Steven Massey](https://github.com/soundandform)'s novel [interpreter topology](/source/README.md), with:
- Aim at Wasm 1.0 spec conformance (not there yet)
- Lot's of bugfixes
- Portability improvements

## Building

See [DEV.md](./DEV.md)
