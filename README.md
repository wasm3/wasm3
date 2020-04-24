<img align="right" width="30%" src="/extra/screenshot-ios.png">

# <img src="/extra/wasm-symbol.svg" width="32" height="32" /> Wasm3

[![WAPM](https://wapm.io/package/vshymanskyy/wasm3/badge.svg)](https://wapm.io/package/vshymanskyy/wasm3) 
[![GitHub issues](https://img.shields.io/github/issues-raw/wasm3/wasm3?style=flat-square&label=issues&color=success)](https://github.com/wasm3/wasm3/issues) 
[![Tests status](https://img.shields.io/github/workflow/status/wasm3/wasm3/tests/master?style=flat-square&logo=github&label=tests)](https://github.com/wasm3/wasm3/actions) 
[![GitHub license](https://img.shields.io/badge/license-MIT-blue?style=flat-square)](https://github.com/wasm3/wasm3)

A high performance WebAssembly interpreter written in C.

**∼ 8x faster** than other known wasm interpreters  
**∼ 4-5x slower** than state of the art wasm `JIT` engines  
**∼ 12x slower** than native execution  
<sub>* Based on [**CoreMark 1.0** benchmark](./docs/Performance.md). Your mileage may vary.</sub>


[![Twitter](https://img.shields.io/twitter/follow/wasm3_engine?style=flat-square&color=1da1f2&label=twitter&logo=twitter)](https://twitter.com/wasm3_engine) 
[![Discord](https://img.shields.io/discord/671415645073702925?style=flat-square&logo=discord&color=7289da&label=discord)](https://discord.gg/qmZjgnd) 
[![Telegram](https://img.shields.io/badge/telegram-chat-0088cc?style=flat-square&logo=telegram)](https://t.me/joinchat/DD8s3xVG8Vj_LxRDm52eTQ)

## Getting Started

Here's an online demo and a small [getting started guide](https://wapm.io/package/vshymanskyy/wasm3):

[![LIVE DEMO](extra/button.png)](https://webassembly.sh/?run-command=wasm3)

## Status

`wasm3` passes the [WebAssembly spec testsuite](https://github.com/WebAssembly/spec/tree/master/test/core) and is able to run many `WASI` apps.

Minimum useful system requirements: **~64Kb** for code and **~10Kb** RAM

`wasm3` runs on a wide range of architectures (`x86`, `x86_64`, `ARM`, `RISC-V`, `PowerPC`, `MIPS`, `Xtensa`, `ARC32`, ...) and [platforms](/platforms):
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/linux.svg" width="18" height="18" /> Linux,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/windows.svg" width="18" height="18" /> Windows,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/apple.svg" width="18" height="18" /> OS X,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/freebsd.svg" width="18" height="18" /> FreeBSD
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/android.svg" width="18" height="18" /> Android,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/apple.svg" width="18" height="18" /> iOS
- <img src="https://cdn.rawgit.com/feathericons/feather/master/icons/wifi.svg" width="18" height="18" /> **OpenWRT**-enabled routers
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/raspberrypi.svg" width="18" height="18" /> Raspberry Pi, Orange Pi and other **SBC**s
- <img src="https://cdn.rawgit.com/feathericons/feather/master/icons/cpu.svg" width="18" height="18" /> **MCU**s: Arduino, ESP8266, ESP32, Particle, ... [see full list](./docs/Hardware.md)
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/mozillafirefox.svg" width="18" height="18" /> <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/googlechrome.svg" width="18" height="18" /> <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/safari.svg" width="18" height="18" /> <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/microsoftedge.svg" width="18" height="18" /> Browsers... yes, using WebAssembly itself!
- <img src="extra/wasm-symbol.svg" width="18" height="18" /> `wasm3` can execute `wasm3` (self-hosting)

## Post-MVP [features](https://github.com/WebAssembly/proposals) support

☑ Sign-extension operators  
☑ Non-trapping float-to-int conversions  
☐ Bulk memory operations  
☐ Multi-value  
☐ Import/Export of Mutable Globals  
☐ Tail call  
☐ Fixed-width SIMD  

## Libraries

Wasm3 can be used as a library for:

[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/cplusplus.svg" width="18" height="18" /> C/C++](https://github.com/wasm3/wasm3) │ 
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/go.svg" width="18" height="18" /> GoLang](https://github.com/matiasinsaurralde/go-wasm3) │ 
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/rust.svg" width="18" height="18" /> Rust](https://github.com/Veykril/wasm3-rs) │ 
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/arduino.svg" width="18" height="18" /> Arduino, PlatformIO, Particle](https://github.com/wasm3/wasm3-arduino)

## Motivation

**Why use a "slow interpreter" versus a "fast JIT"?**

In many situations, speed is not the main concern. Runtime executable size, code density, memory usage, startup latency can be all improved with the interpreter approach. Portability and security are much easier to achieve and maintain. Additionally, development impedance is much lower. A simple library like Wasm3 is easy to compile and integrate into an existing project. (Wasm3 builds in a just few seconds). Finally, on some platforms (i.e. iOS and WebAssembly itself) you can't generate executable code pages in runtime, so JIT is unavailable.

**Why would you want to run WASM on microcontrollers?**

Wasm3 started as a research project and remains so by many means. Evaluating the engine in different environments is part of the research. Given that we have `Lua`, `JS`, `Python`, `Lisp`, `...` running on MCUs, `WebAssembly` is actually a promising alternative. It provides toolchain decoupling as well as a completely sandboxed, well-defined, predictible environment. Among practical use cases we can list `edge computing`, `scripting`, running `IoT rules`, `smart contracts`, etc.


## Further Resources

[Demos](./docs/Demos.md)  
[Build and Development instructions](./docs/Development.md)  
[Supported Hardware](./docs/Hardware.md)  
[Testing & Fuzzing](./docs/Testing.md)  
[Performance](./docs/Performance.md)  
[Interpreter Architecture](./docs/Interpreter.md)  
[Logging](./docs/Diagnostics.md)

### License
This project is released under The MIT License (MIT)
