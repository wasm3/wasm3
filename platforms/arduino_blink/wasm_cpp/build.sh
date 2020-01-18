# Compile
wasicc  -Os                                                   \
        -z stack-size=4096 -Wl,--initial-memory=65536         \
        -Wl,--allow-undefined-file=arduino_wasm_api.syms      \
        -Wl,--strip-all -nostdlib                             \
        -o arduino_app.wasm arduino_app.cpp

# Optimize (optional)
#wasm-opt -O3 arduino_app.wasm -o arduino_app.wasm

# Convert to header
xxd -i arduino_app.wasm > arduino_app.wasm.h
