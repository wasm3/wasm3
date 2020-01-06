#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "m3.h"
#include "m3_api_wasi.h"
#include "m3_api_libc.h"
#include "m3_env.h"

#include "extra/fib32.wasm.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return; }

void run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    size_t fsize = fib32_wasm_len-1;

    IM3Environment env = m3_NewEnvironment ();
    if (!env) FATAL("m3_NewEnvironment failed");

    IM3Runtime runtime = m3_NewRuntime (env, 64*1024, NULL);
    if (!runtime) FATAL("m3_NewRuntime failed");

    IM3Module module;
    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    result = m3_LoadModule (runtime, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    /*
    result = m3_LinkWASI (runtime->modules);
    if (result) FATAL("m3_LinkWASI: %s", result);

    result = m3_LinkLibC (runtime->modules);
    if (result) FATAL("m3_LinkLibC: %s", result);
    */

    IM3Function f;
    result = m3_FindFunction (&f, runtime, "fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    const char* i_argv[2] = { "40", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("Call: %s", result);

    uint64_t value = *(uint64_t*)(runtime->stack);
    printf("Result: %ld\n", value);
}

int  main  (int i_argc, const char * i_argv [])
{
    printf("wasm3 on WASM, build " __DATE__ " " __TIME__ "\n");

    clock_t start = clock();
    run_wasm();
    clock_t end = clock();

    uint32_t elapsed_time = (end - start)*1000 / CLOCKS_PER_SEC ;
    printf("Elapsed: %d ms\n", elapsed_time);

    return 0;
}
