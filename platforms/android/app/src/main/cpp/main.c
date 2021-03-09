//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "m3/wasm3.h"
#include "m3/m3_api_libc.h"

#include "m3/extra/coremark_minimal.wasm.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return; }

void run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)coremark_minimal_wasm;
    size_t fsize = coremark_minimal_wasm_len;

    printf("Loading WebAssembly...\n");

    IM3Environment env = m3_NewEnvironment ();
    if (!env) FATAL("m3_NewEnvironment failed");

    IM3Runtime runtime = m3_NewRuntime (env, 8192, NULL);
    if (!runtime) FATAL("m3_NewRuntime failed");

    IM3Module module;
    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    result = m3_LoadModule (runtime, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    result = m3_LinkLibC (module);
    if (result) FATAL("m3_LinkLibC: %s", result);

    IM3Function f;
    result = m3_FindFunction (&f, runtime, "run");
    if (result) FATAL("m3_FindFunction: %s", result);

    printf("Running CoreMark 1.0...\n");

    result = m3_CallV (f);
    if (result) FATAL("m3_Call: %s", result);

    float value = 0;
    result = m3_GetResultsV (f, &value);
    if (result) FATAL("m3_GetResults: %s", result);

    printf("Result: %0.3f\n", value);
}

int main()
{
    printf("Wasm3 v" M3_VERSION " on Android (" M3_ARCH ")\n");
    printf("Build " __DATE__ " " __TIME__ "\n");

    clock_t start = clock();
    run_wasm();
    clock_t end = clock();

    printf("Elapsed: %ld ms\n", (end - start)*1000 / CLOCKS_PER_SEC);
}
