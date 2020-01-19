//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include "Arduino.h"

#include "m3/m3.h"

// C++ app
#include "../wasm_cpp/app.wasm.h"

// Rust app
//#include "../wasm_rust/app.wasm.h"

// AssemblyScript app
//#include "../wasm_assemblyscript/app.wasm.h"

// TinyGO app. For this, change _start to cwa_main below
//#include "../wasm_tinygo/app.wasm.h"

M3Result  m3_LinkArduino (IM3Runtime runtime);

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return; }

void wasm_task(void*)
{
    M3Result result = m3Err_none;

    IM3Environment env = m3_NewEnvironment ();
    if (!env) FATAL("m3_NewEnvironment failed");

    IM3Runtime runtime = m3_NewRuntime (env, 1024, NULL);
    if (!runtime) FATAL("m3_NewRuntime failed");

    IM3Module module;
    result = m3_ParseModule (env, &module, app_wasm, app_wasm_len-1);
    if (result) FATAL("m3_ParseModule: %s", result);

    result = m3_LoadModule (runtime, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    result = m3_LinkArduino (runtime);
    if (result) FATAL("m3_LinkArduino: %s", result);

    IM3Function f;
    result = m3_FindFunction (&f, runtime, "_start");
    if (result) FATAL("m3_FindFunction: %s", result);

    printf("Running WebAssembly...\n");

    const char* i_argv[1] = { NULL };
    result = m3_CallWithArgs (f, 0, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);

    // Should not arrive here
}

void setup()
{
    Serial.begin(115200);
    delay(100);

    Serial.print("\nWasm3 v" M3_VERSION ", build " __DATE__ " " __TIME__ "\n");

#ifdef ESP32
    // On ESP32, we can launch in a separate thread
    xTaskCreate(&wasm_task, "wasm3", 32768, NULL, 5, NULL);
#else
    wasm_task(NULL);
#endif
}

void loop()
{
    delay(100);
}
