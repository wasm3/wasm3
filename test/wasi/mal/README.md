# mal (Make a Lisp)

[mal](https://github.com/kanaka/mal) is a Lisp interpreter.

`mal.wasm` is mal's own WebAssembly implementation - hand-written `.wam`, no C toolchain
involved - and `mal.mal` is mal written in mal, which it can then run on itself.

### Building

`stepA_mal` is the complete interpreter; the earlier steps are the tutorial. `wamp` is a
macro assembler that expands the `.wam` sources into one `.wat`, which `wasm-as` then
assembles. Note the two workarounds: `ffi-napi` is only needed by mal's own JS host and
no longer builds on current Node, and the `.wat` puts its memory before its imports,
which Binaryen accepted until it started enforcing the order.

```sh
git clone --depth 1 https://github.com/kanaka/mal
cd mal/impls/wasm

npm install --ignore-scripts
make WASM_AS=/path/to/binaryen-108/wasm-as stepA_mal.wasm

cp stepA_mal.wasm .../mal.wasm
```

The self-hosting sources come from `impls/mal` in the same checkout, with the two
`load-file` paths pointed at this directory:

```sh
cp ../mal/core.mal ../mal/env.mal .../
sed 's|\.\./mal/|./|' ../mal/stepA_mal.mal > .../mal.mal
```

### Running

```sh
# REPL:
../../../build/wasm3 mal.wasm

# Self-hosted REPL:
../../../build/wasm3 mal.wasm ./mal.mal

# Fibonacci test:
../../../build/wasm3 mal.wasm ./test-fib.mal 16
987

# Self-hosted Fibonacci test (takes ~10 seconds):
../../../build/wasm3 mal.wasm ./mal.mal ./test-fib.mal 10
55
```
