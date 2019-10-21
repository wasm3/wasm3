#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", __VA_ARGS__); return; }

#include "m3/m3.hpp"

#include "fib.wasm.h"

void run_wasm()
{
    M3Result result = c_m3Err_none;

    u8* wasm = (u8*)fib_wasm;
    u32 fsize = fib_wasm_len-1;

    printf("WASM: %d bytes\n", fsize);

    IM3Module module;
    result = m3_ParseModule (& module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    IM3Runtime env = m3_NewRuntime (8192);

    result = m3_LoadModule (env, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    //m3_LinkFunction (module, "_m3TestOut", "v(iFi)", (void *) m3TestOut);
    //m3_LinkFunction (module, "_m3StdOut", "v(*)", (void *) m3Output);
    //m3_LinkFunction (module, "_m3Export", "v(*i)", (void *) m3Export);
    //m3_LinkFunction (module, "_m3Out_f64", "v(F)", (void *) m3Out_f64);
    //m3_LinkFunction (module, "_m3Out_i32", "v(i)", (void *) m3Out_i32);
    //m3_LinkFunction (module, "_TestReturn", "F(i)", (void *) TestReturn);

    //m3_LinkFunction (module, "abortStackOverflow",	"v(i)", (void *) m3_abort);

    //result = m3_LinkCStd (module);
    //if (result) FATAL("m3_LinkCStd: %s", result);

    m3_PrintRuntimeInfo (env);

    IM3Function f;
    result = m3_FindFunction (&f, env, "__post_instantiate");
    if (! result)
    {
      result = m3_Call (f);
      if (result) FATAL("__post_instantiate: %s", result);
    }

    result = m3_FindFunction (&f, env, "_fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    clock_t start = clock();

    const char* i_argv[2] = { "32", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    clock_t end = clock();

    uint32_t elapsed_time = (end - start)*1000 / CLOCKS_PER_SEC ;
    printf("Elapsed: %d ms\n", elapsed_time);

    m3_PrintProfilerInfo ();

    if (result) FATAL("Call: %s", result);
}

void wasm_task(void*)
{
  run_wasm();
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

    for (;;) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();

}
