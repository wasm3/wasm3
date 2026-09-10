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

**Note:** If this happens with a `wasi_snapshot_preview1.*` function, please report as a bug.

`wasi_unstable.*` is a different matter: that is `snapshot_0`, the 2019 preview that
`wasi_snapshot_preview1` replaced, and Wasm3 no longer provides it. Rebuild the module
against `wasi_snapshot_preview1` - every current toolchain targets it by default.

### `Error: compiling function overran its stack height limit`

Try increasing `d_m3MaxFunctionStackHeight` in `m3_config.h` and rebuilding Wasm3.

### `Error: [Fatal] repl_load: unallocated linear memory`

Your module requires some `Memory`, but doesn't define/export it by itself.  
This happens if module is built by `Emscripten`, or it's a library that is intended to be linked to some other modules.  
Wasm3 currently doesn't support running such modules directly, but you can remove this limitation when embedding Wasm3 into your own app.

### The `.wasm` file cannot be replaced while it is running

Wasm3 maps a module's file rather than reading it, so the module is parsed in place
and keeps pointing into those bytes for as long as it is loaded. Each system is asked
to hold the file still, and they manage very different amounts of it:

- **Windows** refuses another process's open-for-write outright, so the file really
  cannot change while the module runs. Other readers are unaffected, so a second
  Wasm3 on the same module is fine.
- **POSIX** takes a shared `flock`, which stops only a writer that asks for a lock of
  its own - `flock -x`, and the deployment tools that use it. A plain `cp` or `>`
  goes straight through, because POSIX has no portable mandatory locking: Linux
  removed what it had in 5.15, and `O_EXLOCK` is a BSD extension that is advisory as
  well.

So on POSIX, replacing a module by renaming a new file over the old one is safe - the
mapping keeps the old inode - while rewriting the file in place changes the bytes
under a running module.

A file that cannot be mapped at all is read into memory instead, which is a copy of
the whole module and otherwise identical.
