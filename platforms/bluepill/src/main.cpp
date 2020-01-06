
#include "m3/m3.h"
#include "m3/m3_env.h"

#include "m3/extra/fib32.wasm.h"

#include <jee.h>

#define FATAL(msg, ...) { puts("Fatal: " msg "\n"); return; }

void run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    size_t fsize = fib32_wasm_len-1;

    puts("Loading WebAssembly...\n");

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

    puts("Running...\n");

    const char* i_argv[2] = { "24", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);

    long value = *(uint64_t*)(runtime->stack);

    printf("Result: %ld\n", value);
}

PinC<13> led;

int main()
{
  enableSysTick();
  led.mode(Pinmode::out);

  puts("wasm3 on BluePill, build " __DATE__ " " __TIME__ "\n");

  led = 0;
  run_wasm();
  led = 1;

}
