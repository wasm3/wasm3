
#define FATAL(msg, ...) { Serial.println("Fatal: " msg "\n"); return; }

#include "m3.h"
#include "m3_env.h"

#include "extra/fib32.wasm.h"

// Redefine puts
int puts(const char* s) {
    Serial.println(s);
}

void run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    size_t fsize = fib32_wasm_len-1;

    Serial.println("Loading WebAssembly...");

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

    Serial.println("Running...");

    const char* i_argv[2] = { "24", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);

    long value = *(uint64_t*)(runtime->stack);

    Serial.print("Result: ");
    Serial.println(value);
}

void setup()
{
  Serial.begin(115200);
  delay(10);

  Serial.println("Wasm3 v" M3_VERSION " on Particle, build " __DATE__ " " __TIME__);

  u32 start = millis();
  run_wasm();
  u32 end = millis();

  Serial.print("Elapsed: ");
  Serial.print(end - start);
  Serial.println(" ms");
}

void loop()
{
  delay(100);
}
