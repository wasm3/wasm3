## Build using Emscripten

```sh
source /opt/emsdk/emsdk_env.sh --build=Release
mkdir -p build
cd build
cmake -GNinja -DEMSCRIPTEN=1 ..
ninja
```

**Note:** the build uses tail-call WebAssembly extension.

You can convert the generated wasm to wat:
```sh
wasm2wat --enable-tail-call wasm3.wasm > wasm3.wat
```

```sh
emrun --no_browser --no_emrun_detect --port 8080 .
chromium-browser --js-flags="--experimental-wasm-return-call --wasm-opt --wasm-no-bounds-checks --wasm-no-stack-checks" http://localhost:8080/wasm3.html
```

