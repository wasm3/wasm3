# Compile
npx asc app.ts -b app.wasm  \
  -O3z                      \
  --runtime half            \
  --noAssert                \
  --use abort=

# Convert to WAT
#wasm2wat app.wasm -o app.wat

# Convert to C header
xxd -i app.wasm > app.wasm.h
