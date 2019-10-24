#include <stdio.h>

#include "Arduino.h"

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

void setup()
{
  Serial.begin(115200);
  delay(10);
  while (!Serial) {}

  Serial.print("\nwasm3 on ESP8266, build " __DATE__ " " __TIME__ "\n");

  u32 start = millis();
  run_wasm();
  u32 end = millis();

  Serial.print(String("Elapsed: ") +  (end - start) + " ms\n");
}

void loop()
{
  delay(100);
}
