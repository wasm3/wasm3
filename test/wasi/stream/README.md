# STREAM 5.10

https://www.cs.virginia.edu/stream/

Memory bandwidth benchmark. The test suite only checks that it validates its own
results, so the reported rates are informational.

### Building

With [WASI SDK](https://github.com/WebAssembly/wasi-sdk) 34. The defaults are kept:
10M elements per array, which asks the engine for ~229 MiB of linear memory.

```sh
$WASI_SDK_PATH/bin/clang -mcpu=lime1 -g0 -O3 -Wl,--strip-all stream.c -o stream.wasm
```

### Running

```sh
../../../build/wasm3 stream.wasm
```
