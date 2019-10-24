#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return; }

#include "m3/m3.hpp"

#include "fib.wasm.h"

void run_wasm()
{
    M3Result result = c_m3Err_none;

    u8* wasm = (u8*)fib_wasm;
    u32 fsize = fib_wasm_len-1;

    printf("Loading WebAssembly...\n");

    IM3Module module;
    result = m3_ParseModule (& module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    IM3Runtime env = m3_NewRuntime (1024);
    if (!env) FATAL("m3_NewRuntime");

    result = m3_LoadModule (env, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    IM3Function f;
    result = m3_FindFunction (&f, env, "_fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    printf("Running...\n");

    const char* i_argv[2] = { "24", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);
}

void wasm_task(void*)
{
    printf("\nwasm3 on ESP32, build " __DATE__ " " __TIME__ "\n");

    clock_t start = clock();
    run_wasm();
    clock_t end = clock();

    printf("Elapsed: %d ms\n", (end - start)*1000 / CLOCKS_PER_SEC);

    for(;;) {
        vTaskDelay(0xFFFF);
    }
}

extern "C"
void app_main()
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    xTaskCreate(&wasm_task, "wasm_m3", 32768, NULL, 5, NULL);

    for(;;) {
        vTaskDelay(0xFFFF);
    }
}
