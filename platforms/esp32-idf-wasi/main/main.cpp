#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "m3/m3.h"
#include "esp_system.h"
#include "m3/m3_env.h"

#include "m3_api_esp_wasi.h"
#include "wasi_test.wasm.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return; }

static void run_wasm(void)
{
    M3Result result = c_m3Err_none;

    uint8_t* wasm = (uint8_t*)wasi_test_wasm;
    uint32_t fsize = wasi_test_wasm_len-1;

    printf("Loading WebAssembly...\n");
    IM3Environment env = m3_NewEnvironment ();
    if (!env) FATAL("m3_NewEnvironment failed");

    IM3Runtime runtime = m3_NewRuntime (env, 8*1024, NULL);
    if (!runtime) FATAL("m3_NewRuntime failed");

    IM3Module module;
    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    result = m3_LoadModule (runtime, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    result = m3_LinkEspWASI (runtime->modules);
    if (result) FATAL("m3_LinkEspWASI: %s", result);

    IM3Function f;
    result = m3_FindFunction (&f, runtime, "_start");
    if (result) FATAL("m3_FindFunction: %s", result);

    printf("Running...\n");

    const char* i_argv[2] = { "test.wasm", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);

    long value = *(uint64_t*)(runtime->stack);
    printf("Result: %ld\n", value);
}

extern "C" void app_main(void)
{
    printf("\nwasm3 on ESP32, build " __DATE__ " " __TIME__ "\n");

    clock_t start = clock();
    run_wasm();
    clock_t end = clock();

    printf("Elapsed: %ld ms\n", (end - start)*1000 / CLOCKS_PER_SEC);

    sleep(3);
    printf("Restarting...\n\n\n");
    esp_restart();
}
