export PATH=/opt/wasp/build/src/tools:$PATH
wasp wat2wasm --enable-numeric-values -o dino.wasm dino.wat
wasm-opt -Oz -o dino.wasm dino.wasm
