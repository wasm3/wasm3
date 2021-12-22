
M3_SRC_DIR := ../../../source

SOURCES    := ../../app/main.c \
              $(shell find $(M3_SRC_DIR) -type f -name '*.c')

override CFLAGS += -std=c99 -O3 -flto -Dd_m3HasWASI -Wno-error=format-security -Wfatal-errors -I$(M3_SRC_DIR)

all: wasm3

wasm3: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o wasm3 -lm

clean:
	$(RM) wasm3
