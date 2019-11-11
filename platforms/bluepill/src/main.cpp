
#include "m3/m3.h"

#include "m3/extra/fib32.wasm.h"

#include <jee.h>

#define FATAL(msg, ...) { puts("Fatal: " msg "\n"); return; }

void run_wasm()
{
    M3Result result = c_m3Err_none;

    u8* wasm = (u8*)fib32_wasm;
    u32 fsize = fib32_wasm_len-1;

    puts("Loading WebAssembly...\n");

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

    puts("Running...\n");

    const char* i_argv[2] = { "24", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);
}

PinC<13> led;

int main()
{
  enableSysTick();
  led.mode(Pinmode::out);

  puts("wasm3 on Arduino, build " __DATE__ " " __TIME__ "\n");

  led = 0;
  run_wasm();
  led = 1;

  //Serial.print("Elapsed: ");
  //Serial.print(end - start);
  //Serial.println(" ms");
}
