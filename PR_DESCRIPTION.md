# Implement strtoull() for AVR platform (was stubbed to always return 0)

## Problem

On AVR platforms, `strtoull()` in `source/m3_math_utils.h` is stubbed to always return 0. Any Wasm module that relies on string-to-integer conversion (e.g., parsing numeric arguments, configuration values) silently gets wrong results on AVR.

## Root Cause

The AVR toolchain doesn't provide `strtoull()`, so wasm3 includes a stub marked with `//TODO` that unconditionally returns 0. This affects all Wasm modules that use `i64` parsing on AVR targets.

## Fix

Replaced the stub with a proper character-by-character implementation that:
- Skips leading whitespace
- Handles base auto-detection (0x for hex, 0 for octal, otherwise decimal)
- Handles explicit base-16 prefix
- Parses digits correctly for bases 2-16
- Sets the end pointer if provided

## Testing

- Compile wasm3 for an AVR target and run a Wasm module that parses integer strings.
- `strtoull("12345", NULL, 10)` should return 12345 (previously returned 0).
- `strtoull("0xFF", NULL, 0)` should return 255.
- `strtoull("0777", NULL, 0)` should return 511 (octal).

## Impact

Affects all wasm3 users on AVR platforms. Any Wasm module relying on string-to-integer conversion was silently broken.
