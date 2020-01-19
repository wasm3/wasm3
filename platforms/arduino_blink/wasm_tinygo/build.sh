# Prepare
export PATH=$PATH:/usr/local/tinygo/bin
export GOROOT=/opt/go

# Compile
tinygo  build -target wasm                \
        -panic trap -wasm-abi generic     \
        -heap-size 2048                   \
        -o app.wasm app.go

# Optimize (optional)
#wasm-opt -O3 app.wasm -o app.wasm
wasm-strip app.wasm

# Convert to WAT
#wasm2wat app.wasm -o app.wat

# Convert to C header
xxd -i app.wasm > app.wasm.h
