//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include "wasm3.h"

#include "extra/fib32.wasm.h"

#define FATAL(func, msg) {           \
  Serial.print("Fatal: " func ": "); \
  Serial.println(msg); return; }

// Redefine puts
int puts(const char* s) {
    Serial.println(s);
    return 1;
}

void run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    size_t fsize = fib32_wasm_len;

    Serial.println("Loading WebAssembly...");

    IM3Environment env = m3_NewEnvironment ();
    if (!env) FATAL("m3_NewEnvironment", "failed");

    IM3Runtime runtime = m3_NewRuntime (env, 1024, NULL);
    if (!runtime) FATAL("m3_NewRuntime", "failed");

    IM3Module module;
    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) FATAL("m3_ParseModule", result);

    result = m3_LoadModule (runtime, module);
    if (result) FATAL("m3_LoadModule", result);

    IM3Function f;
    result = m3_FindFunction (&f, runtime, "fib");
    if (result) FATAL("m3_FindFunction", result);

    Serial.println("Running...");

    result = m3_CallV (f, 24);
    if (result) FATAL("m3_Call: %s", result);

    uint32_t value = 0;
    result = m3_GetResultsV (f, &value);
    if (result) FATAL("m3_GetResults: %s", result);

    Serial.print("Result: ");
    Serial.println(value);
}

void setup()
{
  Serial.begin(115200);
  delay(10);

  Serial.println();
  Serial.println("Wasm3 v" M3_VERSION " on Particle, build " __DATE__ " " __TIME__);

  uint32_t start = millis();
  run_wasm();
  uint32_t end = millis();

  Serial.print("Elapsed: ");
  Serial.print(end - start);
  Serial.println(" ms");
}

void loop()
{
  delay(100);
}
