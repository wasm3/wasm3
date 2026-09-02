//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include <stdint.h>
#include <stddef.h>

#include "wasm3.h"
#include "m3_env.h"    // for IM3Runtime::memoryLimit

#define FATAL(...) __builtin_trap()

// A module may declare up to 4 GiB of linear memory, and wasm3 allocates the
// initial pages eagerly at load, so a handful of bytes can ask for more than
// the fuzzing engine allows and get killed as an OOM rather than exercising
// anything. Cap what is actually allocated: memory accesses and data segment
// loads are bounded by the allocated length, not the declared page count.
#define d_m3FuzzMemoryLimit  (64*1024*1024)

int LLVMFuzzerTestOneInput (const uint8_t* data, size_t size)
{
    M3Result result = m3Err_none;

    if (size < 8 || size > 256 * 1024) {
        return 0;
    }

    IM3Environment env = m3_NewEnvironment();
    if (env) {
        IM3Runtime runtime = m3_NewRuntime(env, 128, NULL);
        if (runtime) {
            runtime->memoryLimit = d_m3FuzzMemoryLimit;
            IM3Module module     = NULL;

            result = m3_ParseModule(env, &module, data, size);
            if (module) {
                result = m3_LoadModule(runtime, module);
                if (result == 0) {
                    IM3Function f = NULL;

                    result = m3_FindFunction(&f, runtime, "fib");
                    if (f) {
                        m3_CallV(f, 10);
                    }
                }
                // on failure too, the runtime owns the module now
            }

            m3_FreeRuntime(runtime);
        }
        m3_FreeEnvironment(env);
    }

    return 0;
}
