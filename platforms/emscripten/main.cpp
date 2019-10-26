#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "m3.hpp"

#include "extra/fib32.wasm.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", __VA_ARGS__); return; }

void run_wasm()
{
    M3Result result = c_m3Err_none;

    u8* wasm = (u8*)fib32_wasm;
    u32 fsize = fib32_wasm_len-1;

    printf("Loading WebAssembly...\n");

    IM3Module module;
    result = m3_ParseModule (& module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    IM3Runtime env = m3_NewRuntime (8192);

    result = m3_LoadModule (env, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    IM3Function f;
    result = m3_FindFunction (&f, env, "__post_instantiate");
    if (! result)
    {
      result = m3_Call (f);
      if (result) FATAL("__post_instantiate: %s", result);
    }

    result = m3_FindFunction (&f, env, "fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    printf("Running...\n");

    const char* i_argv[2] = { "40", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("Call: %s", result);

}

int  main  (int i_argc, const char * i_argv [])
{
    printf("\nwasm3 on WASM, build " __DATE__ " " __TIME__ "\n");

    clock_t start = clock();
    run_wasm();
    clock_t end = clock();

    uint32_t elapsed_time = (end - start)*1000 / CLOCKS_PER_SEC ;
    printf("Elapsed: %d ms\n", elapsed_time);

    return 0;
}
