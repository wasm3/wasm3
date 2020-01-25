//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "wasm3.h"
#include "m3_api_wasi.h"
#include "m3_api_libc.h"
#include "m3_env.h"
#include <psp2/gxm.h>
#include <psp2/message_dialog.h>
#include <psp2/shellutil.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "message_dialog.h"


#include "extra/fib32.wasm.h"

#include <stdarg.h>



void run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    size_t fsize = fib32_wasm_len-1;

    IM3Environment env = m3_NewEnvironment ();

    IM3Runtime runtime = m3_NewRuntime (env, 64*1024, NULL);

    IM3Module module;
    result = m3_ParseModule (env, &module, wasm, fsize);

    result = m3_LoadModule (runtime, module);

    /*
    result = m3_LinkWASI (runtime->modules);
    if (result) FATAL("m3_LinkWASI: %s", result);

    result = m3_LinkLibC (runtime->modules);
    if (result) FATAL("m3_LinkLibC: %s", result);
    */

    IM3Function f;
    result = m3_FindFunction (&f, runtime, "fib");

    const char* i_argv[2] = { "10", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);


    uint64_t value = *(uint64_t*)(runtime->stack);
    writeLine("Result: %lld\n", value);
    write_to_screen();
}
vita2d_pgf *pgf = NULL;

int  main  (int i_argc, const char * i_argv [])
{
    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x00, 0x00, 0x00, 0xFF));
    pgf = vita2d_load_default_pgf();
    writeLine("Wasm3 v" M3_VERSION " on WASM, build " __DATE__ " " __TIME__ "\n");
    write_to_screen();
    clock_t start = clock();
    run_wasm();
    clock_t end = clock();

    uint32_t elapsed_time = (end - start)*1000 / CLOCKS_PER_SEC ;
    writeLine("Elapsed: %ld ms\n", elapsed_time);
    write_to_screen();
    while(1);
    sceKernelExitProcess(0);
    return 0;
}
