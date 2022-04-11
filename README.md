[![SWUbanner](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/banner-direct.svg)](https://vshymanskyy.github.io/StandWithUkraine)

<img align="right" width="30%" src="/extra/screenshot-ios.png">

# <img src="/extra/wasm-symbol.svg" width="32" height="32" /> Wasm3

[![WAPM](https://wapm.io/package/vshymanskyy/wasm3/badge.svg)](https://wapm.io/package/vshymanskyy/wasm3) 
[![GitHub issues](https://img.shields.io/github/issues-raw/wasm3/wasm3?style=flat-square&label=issues&color=success)](https://github.com/wasm3/wasm3/issues) 
[![Tests status](https://img.shields.io/github/workflow/status/wasm3/wasm3/tests/main?style=flat-square&logo=github&label=tests)](https://github.com/wasm3/wasm3/actions) 
[![Fuzzing Status](https://img.shields.io/badge/oss--fuzz-fuzzing-success?style=flat-square)](https://bugs.chromium.org/p/oss-fuzz/issues/list?can=1&q=proj:wasm3) 
[![GitHub license](https://img.shields.io/badge/license-MIT-blue?style=flat-square)](https://github.com/wasm3/wasm3)

The fastest WebAssembly interpreter, and the most universal runtime.  
<sub>Based on [**CoreMark 1.0**](./docs/Performance.md) and [**independent**](https://00f.net/2021/02/22/webassembly-runtimes-benchmarks) benchmarks. Your mileage may vary.</sub>


[![Twitter](https://img.shields.io/twitter/follow/wasm3_engine?style=flat-square&color=1da1f2&label=twitter&logo=twitter)](https://twitter.com/wasm3_engine) 
[![Discord](https://img.shields.io/discord/671415645073702925?style=flat-square&logo=discord&color=7289da&label=discord)](https://discord.gg/qmZjgnd) 
[![Telegram](https://img.shields.io/badge/telegram-chat-0088cc?style=flat-square&logo=telegram)](https://t.me/joinchat/DD8s3xVG8Vj_LxRDm52eTQ)

## Getting Started

Here's a small [getting started guide](https://wapm.io/package/vshymanskyy/wasm3). Click here to start:

[![LIVE DEMO](extra/button.png)](https://webassembly.sh/?run-command=wasm3)


## Installation

**Please follow the [installation instructions](./docs/Installation.md).**

Wasm3 can also be used as a library for:

[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/python.svg" width="18" height="18" /> Python3](https://github.com/wasm3/pywasm3) │ 
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/rust.svg" width="18" height="18" /> Rust](https://github.com/Veykril/wasm3-rs) │ 
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/cplusplus.svg" width="18" height="18" /> C/C++](https://github.com/wasm3/wasm3) │ 
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/go.svg" width="18" height="18" /> GoLang](https://github.com/matiasinsaurralde/go-wasm3) │ 
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/zig.svg" width="18" height="18" /> Zig](https://github.com/alichay/zig-wasm3) │
[<img src="https://cdn.jsdelivr.net/gh/simple-icons/simple-icons@develop/icons/perl.svg" width="18" height="18" /> Perl](https://metacpan.org/pod/Wasm::Wasm3)  
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/swift.svg" width="18" height="18" /> Swift](https://github.com/shareup/wasm-interpreter-apple) │ 
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/dotnet.svg" width="18" height="18" /> .Net](https://github.com/tana/Wasm3DotNet) │ 
[<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/arduino.svg" width="18" height="18" /> Arduino, PlatformIO, Particle](https://github.com/wasm3/wasm3-arduino) │ [QuickJS](https://github.com/saghul/txiki.js)

## Status

`wasm3` passes the [WebAssembly spec testsuite](https://github.com/WebAssembly/spec/tree/master/test/core) and is able to run many `WASI` apps.

Minimum useful system requirements: **~64Kb** for code and **~10Kb** RAM

`wasm3` runs on a wide range of architectures (`x86`, `x86_64`, `ARM`, `RISC-V`, `PowerPC`, `MIPS`, `Xtensa`, `ARC32`, ...) and [platforms](/platforms):
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/linux.svg" width="18" height="18" /> Linux,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/windows.svg" width="18" height="18" /> Windows,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/apple.svg" width="18" height="18" /> OS X,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/freebsd.svg" width="18" height="18" /> FreeBSD,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/android.svg" width="18" height="18" /> Android,
<img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/apple.svg" width="18" height="18" /> iOS
- <img src="https://cdn.rawgit.com/feathericons/feather/master/icons/wifi.svg" width="18" height="18" /> OpenWrt, Yocto, Buildroot (routers, modems, etc.)
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/raspberrypi.svg" width="18" height="18" /> Raspberry Pi, Orange Pi and other SBCs
- <img src="https://cdn.rawgit.com/feathericons/feather/master/icons/cpu.svg" width="18" height="18" /> MCUs: Arduino, ESP8266, ESP32, Particle, ... [see full list](./docs/Hardware.md)
- <img src="https://cdn.rawgit.com/simple-icons/simple-icons/develop/icons/firefoxbrowser.svg" width="18" height="18" /> Browsers. Yes, using WebAssembly itself!
- <img src="extra/wasm-symbol.svg" width="18" height="18" /> `wasm3` can execute `wasm3` (self-hosting)

## Features

| Webassembly [Core Proposals][WasmProps]  | Extra |
| --- | --- |
| ☑ Import/Export of Mutable Globals           | ☑ Structured execution tracing     |
| ☑ Non-trapping float-to-int conversions      | ☑ Big-Endian systems support       |
| ☑ Sign-extension operators                   | ☑ Wasm and WASI self-hosting       |
| ☑ Multi-value                                | ☑ Gas metering                     |
| ☑ Bulk memory operations (partial support)   | ☑ Linear memory limit (< 64KiB)    |
| ☐ Multiple memories                          |
| ☐ Reference types                            |
| ☐ Tail call optimization                     |
| ☐ Fixed-width SIMD                           |
| ☐ Exception handling                         |

## Motivation

**Why use a "slow interpreter" versus a "fast JIT"?**

In many situations, speed is not the main concern. Runtime executable size, memory usage, startup latency can be improved with the interpreter approach. Portability and security are much easier to achieve and maintain. Additionally, development impedance is much lower. A simple library like Wasm3 is easy to compile and integrate into an existing project. (Wasm3 builds in a just few seconds). Finally, on some platforms (i.e. iOS and WebAssembly itself) you can't generate executable code pages in runtime, so JIT is unavailable.

**Why would you want to run WASM on embedded devices?**

Wasm3 started as a research project and remains so by many means. Evaluating the engine in different environments is part of the research. Given that we have `Lua`, `JS`, `Python`, `Lisp`, `...` running on MCUs, `WebAssembly` is actually a promising alternative. It provides toolchain decoupling as well as a completely sandboxed, well-defined, predictable environment. Among practical use cases we can list `edge computing`, `scripting`, `plugin systems`, running `IoT rules`, `smart contracts`, etc.

## Used by

[<img src="https://wasmcloud.dev/images/logo.png" height="32" />](https://wasmcloud.dev/)　
[<img src="/extra/wowcube.png" height="32" />](https://wowcube.com/)　
[<img src="/extra/scailable.png" height="32" />](https://scailable.net/)　
[<img src="/extra/blynk.png" height="32" />](https://blynk.io/)　
[<img src="/extra/iden3.svg" height="32" />](https://www.iden3.io/)　
[<img src="https://user-images.githubusercontent.com/1506708/114701856-069ce700-9d2c-11eb-9b72-9ce2dfd9f0fb.png" height="32" />](https://github.com/kateinoigakukun/wasmic-ios)　
[<img src="https://www.balena.io/avatar.png" height="32" />](https://github.com/balena-io-playground/balena-wasm3)　
[<img src="https://krustlet.dev/images/horizontal.svg" height="32" />](https://github.com/deislabs/krustlet-wasm3)　
[<img src="/extra/shareup_app.svg" height="24" />](https://shareup.app/blog/introducing-shareup/)

## Further Resources

[Demos](./docs/Demos.md)  
[Installation instructions](./docs/Installation.md)  
[Cookbook](./docs/Cookbook.md)  
[Troubleshooting](./docs/Troubleshooting.md)  
[Build and Development instructions](./docs/Development.md)  
[Supported Hardware](./docs/Hardware.md)  
[Testing & Fuzzing](./docs/Testing.md)  
[Performance](./docs/Performance.md)  
[Interpreter Architecture](./docs/Interpreter.md)  
[Logging](./docs/Diagnostics.md)  
[Awesome WebAssembly Tools](https://github.com/vshymanskyy/awesome-wasm-tools/blob/main/README.md)

### License
This project is released under The MIT License (MIT)


[WasmProps]: https://github.com/WebAssembly/proposals/blob/master/finished-proposals.md  "WebAssembly Finished Proposals"
