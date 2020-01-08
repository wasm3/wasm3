//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include <stdint.h>
#include <stddef.h>

#include "m3.h"
#include "m3_api_wasi.h"
#include "m3_api_libc.h"
#include "m3_env.h"


#define FATAL(...) __builtin_trap()


int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    M3Result result = m3Err_none;


    IM3Environment env = m3_NewEnvironment ();
    if (env) {
        IM3Runtime runtime = m3_NewRuntime (env, 64*1024, NULL);
        if (runtime) {
            IM3Module module = NULL;
            result = m3_ParseModule (env, &module, data, size);
            if (module) {
                result = m3_LoadModule (runtime, module);
                if (result == 0) {
                    /*
                    result = m3_LinkWASI (runtime->modules);
                    if (result) FATAL("m3_LinkWASI: %s", result);

                    result = m3_LinkLibC (runtime->modules);
                    if (result) FATAL("m3_LinkLibC: %s", result);
                    */
                    IM3Function f = NULL;
                    result = m3_FindFunction (&f, runtime, "fib");
                    if (f) {
                        const char* i_argv[1] = { "10", NULL };
                        m3_CallWithArgs (f, 1, i_argv);
                    }
                }

            }

            m3_FreeRuntime(runtime);
        }
        m3_FreeEnvironment(env);
    }

    return 0;
}
