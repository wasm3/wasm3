#ifndef wasm_api_h
#define wasm_api_h

#include <stdint.h>

#define WASM_EXPORT               __attribute__((used)) __attribute__((visibility ("default")))
#define WASM_EXPORT_AS(NAME)      WASM_EXPORT __attribute__((export_name(NAME)))
#define WASM_IMPORT(MODULE,NAME)  __attribute__((import_module(MODULE))) __attribute__((import_name(NAME)))

//__attribute__((weak))

WASM_IMPORT("wasm3", "raw_sum")
int64_t wasm3_raw_sum        (int32_t val1, int32_t val2, int32_t val3, int32_t val4);

#endif
