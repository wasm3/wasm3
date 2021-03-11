# mal (Make a Lisp)

[mal](https://github.com/kanaka/mal) is a Lisp interpreter.

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
