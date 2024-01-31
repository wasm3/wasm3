NAME=test_prog_multivalue
SRC=$(NAME).cpp
WASM=$(NAME).wasm

CLANG_FLAGS= -std=c++17 --target=wasm32 -O3 -nostdlib
CLANG_MULTIVAL_ARGS = -mmultivalue -Xclang -target-abi -Xclang experimental-mv
LINKER_ARGS= -Wl,--global-base=0 -Wl,--no-entry -Wl,--export-dynamic

all: $(WASM)

clean:
	rm -f $(WASM)

.PHONY: all clean

$(WASM): $(SRC)
	clang++ $< -o $@ $(CLANG_FLAGS) $(CLANG_MULTIVAL_ARGS) $(LINKER_ARGS)
