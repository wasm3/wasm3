
#define FATAL(msg, ...) { Serial.println("Fatal: " msg "\n"); return; }

#include "m3.h"
#include "m3_env.h"
#include "m3_compile.h"

#include "extra/fib32.wasm.h"

// Redefine puts
int puts(const char* s) {
    Serial.println(s);
}

void run_wasm()
{
    M3Result result = c_m3Err_none;

    u8* wasm = (u8*)fib32_wasm;
    u32 fsize = fib32_wasm_len-1;

    Serial.println("Loading WebAssembly...");

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

    Serial.println("Running...");

    const char* i_argv[2] = { "40", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);

    m3stack_t stack = (m3stack_t)(env->stack);

    Serial.print("Result: ");
    Serial.println((long)stack[0]);
}

void setup()
{
  Serial.begin(115200);
  delay(10);

  Serial.println("wasm3 on Particle, build " __DATE__ " " __TIME__);

  Serial.println(sizeof(M3Compilation));

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
