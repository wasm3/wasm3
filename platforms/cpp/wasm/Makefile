NAME=test_prog
SRC=$(NAME).c
WASM=$(NAME).wasm
HEADER=$(NAME).wasm.h
EMCC_FLAGS=-s STANDALONE_WASM -s ERROR_ON_UNDEFINED_SYMBOLS=0 -O3

all: $(HEADER)

clean:
	rm -f $(HEADER) $(WASM)

.PHONY: all clean

$(WASM): $(SRC)
	emcc $< -o $@ $(EMCC_FLAGS)

$(HEADER): $(WASM)
	xxd -i $< >$@
