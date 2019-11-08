# Wasm3

This is an (experimental) high performance WebAssembly interpreter written in C.

## Status
Currently it runs on:
- OS X, Linux, Windows, OpenWRT-enabled routers, Raspberry Pi  
- microcontrollers: ESP32, ESP8266, nRF52, nRF51, Blue Pill, Kendryte 210, MXChip AZ3166, Arduino MKR1010 (and similar stuff)  
- furthermore, it can run in browsers. Yes, using WebAssembly itself!

Minimal system requirements are ~64Kb for code and ~10Kb RAM.

Wasm3 is built on top of **Steven Massey**'s novel interpreter topology, with
- aim at Wasm 1.0 spec conformance (not there yet)
- lot's of fixes
- portability improvements

## Performance

`TODO`
