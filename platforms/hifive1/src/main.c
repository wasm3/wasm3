#include <stdio.h>
#include <time.h>

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return; }

#include "m3/m3.h"

#include "m3/extra/fib32.wasm.h"

void run_wasm()
{
    M3Result result = c_m3Err_none;

    u8* wasm = (u8*)fib32_wasm;
    u32 fsize = fib32_wasm_len-1;

    printf("Loading WebAssembly...\n");

    IM3Module module;
    result = m3_ParseModule (& module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    IM3Runtime env = m3_NewRuntime (1024);
    if (!env) FATAL("m3_NewRuntime");

    result = m3_LoadModule (env, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    IM3Function f;
    result = m3_FindFunction (&f, env, "fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    printf("Running...\n");

    const char* i_argv[2] = { "24", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);
}

int main() {
    printf("\nwasm3 on HiFive1, build " __DATE__ " " __TIME__ "\n");
    // TODO: fix clock (shows wrong time)
    clock_t start = clock();
    run_wasm();
    clock_t end = clock();

    printf("Elapsed: %d ms\n", (end - start)*1000 / CLOCKS_PER_SEC);
}
