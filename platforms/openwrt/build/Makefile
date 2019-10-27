
M3_SRC_DIR := ../../../source

SOURCES    := main.c \
              $(shell find $(M3_SRC_DIR) -type f -name '*.c')

all: wasm3

override CFLAGS += -std=c99 -O3 -flto -Wfatal-errors -fstrict-aliasing -I$(M3_SRC_DIR)
wasm3: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o wasm3 -lm

clean:
	$(RM) wasm3
