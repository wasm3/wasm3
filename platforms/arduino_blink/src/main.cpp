//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include "Arduino.h"

#include "m3/m3.h"
#include "m3/m3_env.h"

/*
 * Configuration
 */

// Redefine the default LED pin here, if needed
#define LED_BUILTIN   13

#define WASM_STACK_SLOTS  1024
#define NATIVE_STACK_SIZE 32768

// Most devices that cannot allocate a 64KiB wasm page
#define WASM_MEMORY_LIMIT 2048

/*
 * WebAssembly app
 */

// C++ app
#include "../wasm_cpp/app.wasm.h"

// Rust app
//#include "../wasm_rust/app.wasm.h"

// AssemblyScript app
//#include "../wasm_assemblyscript/app.wasm.h"

// TinyGO app. For this, change _start to cwa_main below
//#include "../wasm_tinygo/app.wasm.h"

M3Result  LinkArduino  (IM3Runtime runtime);

/*
 * Engine start, liftoff!
 */

#define FATAL(func, msg) { Serial.print("Fatal: " func " "); Serial.println(msg); return; }

void wasm_task(void*)
{
    M3Result result = m3Err_none;

    IM3Environment env = m3_NewEnvironment ();
    if (!env) FATAL("NewEnvironment", "failed");

    IM3Runtime runtime = m3_NewRuntime (env, WASM_STACK_SLOTS, NULL);
    if (!runtime) FATAL("NewRuntime", "failed");

#ifdef WASM_MEMORY_LIMIT
    runtime->memoryLimit = WASM_MEMORY_LIMIT;
#endif

    IM3Module module;
    result = m3_ParseModule (env, &module, app_wasm, app_wasm_len-1);
    if (result) FATAL("ParseModule", result);

    result = m3_LoadModule (runtime, module);
    if (result) FATAL("LoadModule", result);

    result = LinkArduino (runtime);
    if (result) FATAL("LinkArduino", result);

    IM3Function f;
    result = m3_FindFunction (&f, runtime, "_start");
    if (result) FATAL("FindFunction", result);

    Serial.println("Running WebAssembly...");

    const char* i_argv[1] = { NULL };
    result = m3_CallWithArgs (f, 0, i_argv);

    if (result) FATAL("CallWithArgs", result);

    // Should not arrive here
}

void setup()
{
    Serial.begin(115200);
    delay(100);

    Serial.println("\nWasm3 v" M3_VERSION ", build " __DATE__ " " __TIME__);

#ifdef ESP32
    // On ESP32, we can launch in a separate thread
    xTaskCreate(&wasm_task, "wasm3", NATIVE_STACK_SIZE, NULL, 5, NULL);
#else
    wasm_task(NULL);
#endif
}

void loop()
{
    delay(100);
}
