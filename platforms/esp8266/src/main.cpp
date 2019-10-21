#include <stdio.h>

#include "Arduino.h"
#include <ESP8266WiFi.h>

#define FATAL(msg, ...) { printf("Fatal: " msg "\n", __VA_ARGS__); return; }

#include "m3/m3.hpp"

#include "fib.wasm.h"

/*
extern "C" {

size_t stack_start;
size_t stack_min;

#define STACK_INIT()   {				\
	char stack;							\
	stack_min = stack_start = (size_t)&stack;		\
}

void stackCheck() {
	char stack;
	size_t addr = (size_t)&stack;
	stack_min = min(stack_min, addr);
}

void stackPrintMax() {
	printf("Max stack size: %d\n", stack_start - stack_min);
}



#ifdef TRACK_STACK

void c_delay() {
	static int ctnr = 0;

	if ((ctnr++ % 512) == 0) {
		//ESP.wdtFeed();
		//
		stackPrintMax();
	}
}
#endif



void c_delay() {
}

}
*/

void run_wasm()
{
	printf("Free heap: %d\n", ESP.getFreeHeap());

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

    u32 start = millis();

    const char* i_argv[2] = { "24", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    u32 end = millis();

    printf("Elapsed: %d ms\n", end - start);

	printf("Free heap: %d\n", ESP.getFreeHeap());

    if (result) FATAL("Call: %s", result);

}

void setup()
{
  Serial.begin(115200);
  delay(10);
  WiFi.mode(WIFI_OFF);

  run_wasm();
}

void loop()
{
  delay(100);
}
