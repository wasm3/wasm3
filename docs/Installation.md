# Wasm3 installation

Prebuilt binaries live on the [latest release page](https://github.com/wasm3/wasm3/releases/latest).
They are static, so nothing has to be installed alongside them.

## Windows

Download [`wasm3-win-x64.exe`](https://github.com/wasm3/wasm3/releases/latest/download/wasm3-win-x64.exe) or [`wasm3-win-x86.exe`](https://github.com/wasm3/wasm3/releases/latest/download/wasm3-win-x86.exe).

## Linux

Download [`wasm3-linux-x64.elf`](https://github.com/wasm3/wasm3/releases/latest/download/wasm3-linux-x64.elf) and mark it executable:
```sh
chmod +x wasm3-linux-x64.elf
```

> [!IMPORTANT]
> Other architectures - Arm, AArch64, MIPS, PowerPC, RISC-V, s390x,
> LoongArch, m68k, MicroBlaze, SH4 - ship together in
> [`wasm3-linux-other.tar.gz`](https://github.com/wasm3/wasm3/releases/latest/download/wasm3-linux-other.tar.gz),
> each linked against musl the same way.

## macOS

Use [Homebrew](https://brew.sh) to build and install automatically:
```sh
brew install wasm3
```

## Cosmopolitan / Actually Portable Executable
[APE](https://github.com/jart/cosmopolitan) is a polyglot format that runs natively on Linux + Mac + Windows + FreeBSD + OpenBSD + NetBSD + BIOS.  
Download [`wasm3-cosmopolitan.com`](https://github.com/wasm3/wasm3/releases/latest/download/wasm3-cosmopolitan.com).

## OpenWRT
Follow instructions [here](https://github.com/wasm3/wasm3-openwrt-packages).

## Arduino / PlatformIO / Particle.io library
Find `Wasm3` in the `Library Manager` and install it.

## Python module
```sh
pip3 install pywasm3
```

