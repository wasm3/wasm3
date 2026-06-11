# Wasm3 troubleshooting

### `Error: [trap] stack overflow`

Increase the stack size:
```sh
wasm3 --stack-size 1000000 file.wasm
```

### `Error: missing imported function`

This means that the runtime doesn't provide a specific function, needed for your module execution.  
You need to implement the required functions, and re-build Wasm3.  
Alternatively, you can use Python to define your environment. Check out [`pywasm3`](https://pypi.org/project/pywasm3/) module.

**Note:** If this happens with `WASI` functions like `wasi_unstable.*` or `wasi_snapshot_preview1.*`, please report as a bug.

### `Error: compiling function overran its stack height limit`

Try increasing `d_m3MaxFunctionStackHeight` in `m3_config.h` and rebuilding Wasm3.

### `Error: [Fatal] repl_load: unallocated linear memory`

Your module requires some `Memory`, but doesn't define/export it by itself.  
This happens if module is built by `Emscripten`, or it's a library that is intended to be linked to some other modules.  
Wasm3 currently doesn't support running such modules directly, but you can remove this limitation when embedding Wasm3 into your own app.
