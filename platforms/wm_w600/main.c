#include "wm_include.h"

#include "m3/m3.h"

#include "fib.wasm.h"

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", __VA_ARGS__); return; }

unsigned int millis(void)
{
    return tls_os_get_time()*2;
}

void run_wasm()
{
    M3Result result = c_m3Err_none;

    u8* wasm = (u8*)fib_wasm;
    u32 fsize = fib_wasm_len-1;

    IM3Module module;
    result = m3_ParseModule (& module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    IM3Runtime env = m3_NewRuntime (8192);

    result = m3_LoadModule (env, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    IM3Function f;
    result = m3_FindFunction (&f, env, "__post_instantiate");
    if (! result)
    {
      result = m3_Call (f);
      if (result) FATAL("__post_instantiate: %s", result);
    }

    result = m3_FindFunction (&f, env, "_fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    u32 start = millis();

    const char* i_argv[2] = { "32", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    u32 end = millis();

    unsigned elapsed_time = (end - start);
    printf("Elapsed: %d ms\n", elapsed_time);

    if (result) FATAL("Call: %s", result);
}


#define USER_TASK_STK_SIZE      32768
#define USER_TASK_PRIO          32

static u8 user_task_stk[USER_TASK_STK_SIZE];

void wasm3_task(void *data)
{
    run_wasm();

    for (int i=0; i<USER_TASK_STK_SIZE; i++) {
      if (user_task_stk[i] != 0xA5) {
        printf("Stack used: %d\n", USER_TASK_STK_SIZE-i);
        break;
      }
    }
}

void UserMain(void)
{
    /* paint the stack */
    for (int i=0; i<USER_TASK_STK_SIZE; i++) {
      user_task_stk[i] = 0xA5;
    }

    printf("\nwasm3 on W600, build %s %s\n", __DATE__, __TIME__);
    /* create task */
    tls_os_task_create(NULL,
            "wasm3",
            wasm3_task,
            (void*) 0,
            (void*) &user_task_stk,
            USER_TASK_STK_SIZE,
            USER_TASK_PRIO,
            0);
}

