# WebAssembly validation

Wasm3 performs many parsing and runtime checks, but it is not a complete WebAssembly validator. Some malformed or invalid modules may be rejected late, fail with a runtime error, or hit rough edges before the full validation work is merged.

## Untrusted modules

Do not run untrusted `.wasm` modules with Wasm3 unless they have been validated by an external tool first.

Useful external validators include:

- `wasm-validate` from [WABT](https://github.com/WebAssembly/wabt)
- `wasm-tools validate` from [wasm-tools](https://github.com/bytecodealliance/wasm-tools)
- `wat2wasm` / `wast2json` from WABT for text-format and spec-test inputs

If a module does not pass validation with an external validator, do not feed it to Wasm3.

## Current status

A more complete validator is being developed in the `validation` branch. Cheap structural checks are intended to remain available even when more expensive validation is disabled. If validation can be disabled in a particular build or configuration, treat that mode as suitable only for trusted inputs.

When reporting validation-related issues, include the offending module or a minimal reproducer, whether it validates with external tools, and the Wasm3 revision/build options.
