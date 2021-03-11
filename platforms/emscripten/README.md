## Build using Emscripten

In root:

```sh
source /opt/emsdk/emsdk_env.sh --build=Release
mkdir -p build
cd build
cmake -GNinja -DEMSCRIPTEN=1 ..
ninja
```

**Note:**

To enable WebAssembly extensions:
```sh
cmake -GNinja -DEMSCRIPTEN=1 -DWASM_EXT=1 ..
```

You can convert the generated wasm to wat to see the effect:
```sh
wasm2wat --enable-tail-call --enable-bulk-memory wasm3.wasm > wasm3.wat
```

Running `tail-call` version will require Chrome with experimental flags:
```sh
emrun --no_browser --no_emrun_detect --port 8080 .
chrome --js-flags="--experimental-wasm-return-call --wasm-opt" --no-sandbox http://localhost:8080/wasm3.html
```

Or use Node.js:
```sh
node --experimental-wasm-return-call --wasm-opt ./wasm3.js
```

