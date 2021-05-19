//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include "Arduino.h"

#include <stdio.h>
#include "wasm3.h"

#include "extra/fib32.wasm.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return; }

void run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    uint32_t fsize = fib32_wasm_len;

    printf("Loading WebAssembly...\n");
    IM3Environment env = m3_NewEnvironment ();
    if (!env) FATAL("m3_NewEnvironment failed");

    IM3Runtime runtime = m3_NewRuntime (env, 1024, NULL);
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

    result = m3_CallV(f, 24);
    if (result) FATAL("m3_Call: %s", result);

    uint32_t value = 0;
    result = m3_GetResultsV (f, &value);
    if (result) FATAL("m3_GetResults: %s", result);

    printf("Result: %d\n", value);
}

void setup()
{
  Serial.begin(115200);
  delay(10);

  Serial.print("\nWasm3 v" M3_VERSION " on ESP8266, build " __DATE__ " " __TIME__ "\n");

  u32 start = millis();
  run_wasm();
  u32 end = millis();

  Serial.print(String("Elapsed: ") +  (end - start) + " ms\n");
}

void loop()
{
  delay(100);
}
