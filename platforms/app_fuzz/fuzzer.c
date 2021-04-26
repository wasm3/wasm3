//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include <stdint.h>
#include <stddef.h>

#include "wasm3.h"

#define FATAL(...) __builtin_trap()

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    M3Result result = m3Err_none;

    if (size < 8 || size > 256*1024) {
        return 0;
    }

    IM3Environment env = m3_NewEnvironment ();
    if (env) {
        IM3Runtime runtime = m3_NewRuntime (env, 128, NULL);
        if (runtime) {
            IM3Module module = NULL;
            result = m3_ParseModule (env, &module, data, size);
            if (module) {
                result = m3_LoadModule (runtime, module);
                if (result == 0) {
                    IM3Function f = NULL;
                    result = m3_FindFunction (&f, runtime, "fib");
                    /* TODO:
                    if (f) {
                        m3_CallV (f, 10);
                    }*/
                } else {
                    m3_FreeModule (module);
                }
            }

            m3_FreeRuntime(runtime);
        }
        m3_FreeEnvironment(env);
    }

    return 0;
}
