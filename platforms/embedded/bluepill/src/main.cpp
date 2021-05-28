//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include "wasm3.h"

#include "extra/fib32.wasm.h"

#include <stdio.h>
#include <jee.h>

#define FATAL(func, msg) {           \
  puts("Fatal: " func ": ");         \
  puts(msg); return; }

void run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    size_t fsize = fib32_wasm_len;

    puts("Loading WebAssembly...");

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

    puts("Running...");

    result = m3_CallV (f, 24);
    if (result) FATAL("m3_Call", result);

    uint32_t value = 0;
    result = m3_GetResultsV (f, &value);
    if (result) FATAL("m3_GetResults", result);

    char buff[32];
    itoa(value, buff, 10);

    puts("Result: ");
    puts(buff);
    puts("\n");
}

PinC<13> led;

int main()
{
  enableSysTick();
  led.mode(Pinmode::out);

  puts("Wasm3 v" M3_VERSION " on BluePill, build " __DATE__ " " __TIME__ "\n");

  led = 0;
  run_wasm();
  led = 1;

}
