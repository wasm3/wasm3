//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <emscripten.h>

#include "wasm3.h"
#include "m3_env.h"

IM3Environment env;

EMSCRIPTEN_KEEPALIVE
void init() {
    env = m3_NewEnvironment ();
    if (!env) return;
}

EMSCRIPTEN_KEEPALIVE
IM3Runtime new_runtime() {
    return m3_NewRuntime (env, 64*1024, NULL);
}

EMSCRIPTEN_KEEPALIVE
void free_runtime(IM3Runtime runtime) {
    m3_FreeRuntime (runtime);
}

EMSCRIPTEN_KEEPALIVE
void load(IM3Runtime runtime, uint8_t* wasm, size_t fsize) {
    M3Result result = m3Err_none;

    IM3Module module;
    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) return;

    result = m3_LoadModule (runtime, module);
    if (result) return;
}

EMSCRIPTEN_KEEPALIVE
uint32_t call(IM3Runtime runtime, int argc, const char* argv[]) {
    M3Result result = m3Err_none;

    IM3Function f;
    result = m3_FindFunction (&f, runtime, argv[0]);
    if (result) return -1;

    result = m3_CallArgv (f, argc-1, argv+1);
    if (result) return -2;

    return *(uint64_t*)(runtime->stack);
}
