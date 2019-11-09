#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "m3.h"
#include "m3_host.h"
#include "m3_env.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", ##__VA_ARGS__); return 1; }

int  main  (int i_argc, const char * i_argv [])
{
    M3Result result = c_m3Err_none;

    if (i_argc < 3) FATAL("not enough arguments");

    u8* wasm = NULL;
    u32 fsize = 0;

    //printf("Loading WebAssembly...\n");

    FILE* f = fopen (i_argv[1], "rb");
    if (f)
    {
        fseek (f, 0, SEEK_END);
        fsize = ftell(f);
        fseek (f, 0, SEEK_SET);

        if (fsize > 1000000) FATAL("file is too big");

        wasm = (u8*) malloc(fsize);
        fread (wasm, 1, fsize, f);
        fclose (f);
    }

    IM3Module module;
    result = m3_ParseModule (& module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    IM3Runtime env = m3_NewRuntime (4096);
    if (!env) FATAL("m3_NewRuntime");

    result = m3_LoadModule (env, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    /*
    m3_LinkFunction (module, "_m3TestOut", "v(iFi)", (void *) m3TestOut);
    m3_LinkFunction (module, "_m3StdOut", "v(*)", (void *) m3Output);
    m3_LinkFunction (module, "_m3Export", "v(*i)", (void *) m3Export);
    m3_LinkFunction (module, "_m3Out_f64", "v(F)", (void *) m3Out_f64);
    m3_LinkFunction (module, "_m3Out_i32", "v(i)", (void *) m3Out_i32);
    m3_LinkFunction (module, "_TestReturn", "F(i)", (void *) TestReturn);

    m3_LinkFunction (module, "abortStackOverflow",    "v(i)",        (void *) m3_abort);

    result = m3_LinkCStd (module); if (result) FATAL("m3_LinkCStd: %s", result);

    m3_PrintRuntimeInfo (env);
    */

    IM3Function func;
    result = m3_FindFunction (& func, env, "__post_instantiate");

    if (not result)
    {
        result = m3_Call (func);
        if (result) FATAL("__post_instantiate failed: %s", result);
    }

    const char* fname = i_argv[2];

    result = m3_FindFunction (& func, env, fname);
    if (result) FATAL("m3_FindFunction: %s", result);

    //printf("Running...\n");

    if (!strcmp(fname, "_main") || !strcmp(fname, "main")) {
        i_argc -= 2;
        i_argv += 2;
        result = m3_CallMain (func, i_argc, i_argv);

        m3stack_t stack = (m3stack_t)(env->stack);
        return stack[0];
    } else {
        i_argc -= 3;
        i_argv += 3;
        result = m3_CallWithArgs (func, i_argc, i_argv);
    }

    m3_FreeRuntime (env);

    free (wasm);

    if (result) FATAL("m3_Call: %s", result);

    printf ("\n");

    return 0;
}
