#ifndef wasm_api_h
#define wasm_api_h

#include <stdint.h>

#define WASM_EXPORT               __attribute__((used)) __attribute__((visibility ("default")))
#define WASM_EXPORT_AS(NAME)      WASM_EXPORT __attribute__((export_name(NAME)))
#define WASM_IMPORT(MODULE,NAME)  __attribute__((import_module(MODULE))) __attribute__((import_name(NAME)))

#endif
