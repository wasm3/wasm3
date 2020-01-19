# Compile
wasicc  -Os                                                   \
        -z stack-size=4096 -Wl,--initial-memory=65536         \
        -Wl,--allow-undefined-file=arduino_api.syms           \
        -Wl,--strip-all -nostdlib                             \
        -o app.wasm app.cpp

# Optimize (optional)
#wasm-opt -O3 app.wasm -o app.wasm
wasm-strip app.wasm

# Convert to WAT
#wasm2wat app.wasm -o app.wat

# Convert to C header
xxd -i app.wasm > app.wasm.h
