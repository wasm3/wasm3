//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include "esp_system.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "wasm3.h"
#include "m3_env.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "extra/fib32.wasm.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return; }

static const char* TAG = "example";

static void run_wasm(void)
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    uint32_t fsize = fib32_wasm_len-1;

    printf("Loading WebAssembly...\n");
    IM3Environment env = m3_NewEnvironment ();
    if (!env) FATAL("m3_NewEnvironment failed");

    IM3Runtime runtime = m3_NewRuntime (env, 4096, NULL);
    if (!runtime) FATAL("m3_NewRuntime failed");

    IM3Module module;
    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    result = m3_LoadModule (runtime, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    IM3Function f;
    result = m3_FindFunction (&f, runtime, "fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    printf("Running...\n");

    const char* i_argv[2] = { "30", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);

    long value = *(uint64_t*)(runtime->stack);
    printf("Result: %ld\n", value);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "stack used: %d", CONFIG_ESP_MAIN_TASK_STACK_SIZE - uxTaskGetStackHighWaterMark(NULL));

    printf("\nWasm3 v" M3_VERSION " on ESP32, build " __DATE__ " " __TIME__ "\n");

    clock_t start = clock();
    run_wasm();
    clock_t end = clock();

    printf("Elapsed: %ld ms\n", (end - start)*1000 / CLOCKS_PER_SEC);
    ESP_LOGI(TAG, "stack used: %d", CONFIG_ESP_MAIN_TASK_STACK_SIZE - uxTaskGetStackHighWaterMark(NULL));

    sleep(3);
    printf("Restarting...\n\n\n");
    esp_restart();
}
