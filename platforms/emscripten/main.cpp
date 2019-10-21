#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "m3.hpp"

#include "fib.wasm.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", __VA_ARGS__); return; }

void run_wasm()
{
    M3Result result = c_m3Err_none;

    u8* wasm = (u8*)fib_wasm;
    u32 fsize = fib_wasm_len-1;

    printf("WASM: %d bytes\n", fsize);

    IM3Module module;
    result = m3_ParseModule (& module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    IM3Runtime env = m3_NewRuntime (8192);

    result = m3_LoadModule (env, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    //m3_LinkFunction (module, "_m3TestOut", "v(iFi)", (void *) m3TestOut);
    //m3_LinkFunction (module, "_m3StdOut", "v(*)", (void *) m3Output);
    //m3_LinkFunction (module, "_m3Export", "v(*i)", (void *) m3Export);
    //m3_LinkFunction (module, "_m3Out_f64", "v(F)", (void *) m3Out_f64);
    //m3_LinkFunction (module, "_m3Out_i32", "v(i)", (void *) m3Out_i32);
    //m3_LinkFunction (module, "_TestReturn", "F(i)", (void *) TestReturn);

    //m3_LinkFunction (module, "abortStackOverflow",	"v(i)", (void *) m3_abort);

    //result = m3_LinkCStd (module);
    //if (result) FATAL("m3_LinkCStd: %s", result);

    m3_PrintRuntimeInfo (env);

    IM3Function f;
    result = m3_FindFunction (&f, env, "__post_instantiate");
    if (! result)
    {
      result = m3_Call (f);
      if (result) FATAL("__post_instantiate: %s", result);
    }

    result = m3_FindFunction (&f, env, "_fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    clock_t start = clock();

    const char* i_argv[2] = { "40", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    clock_t end = clock();

    uint32_t elapsed_time = (end - start)*1000 / CLOCKS_PER_SEC ;
    printf("Elapsed: %d ms\n", elapsed_time);

    if (result) FATAL("Call: %s", result);

}

int  main  (int i_argc, const char * i_argv [])
{
	run_wasm();
	return 0;
}
