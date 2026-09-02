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

You can convert the generated wasm to wat to see the effect:
```sh
wasm2wat --enable-tail-call --enable-bulk-memory wasm3.wasm > wasm3.wat
```

The build uses the tail-call proposal, so it needs a host that has it - shipped in Chrome
112, Firefox 121, Safari 18.2 and Node 22, no flags required:

```sh
emrun --no_browser --no_emrun_detect --port 8080 .
```
then open `http://localhost:8080/wasm3.html`.

Or use Node.js:
```sh
node ./wasm3.js
```

