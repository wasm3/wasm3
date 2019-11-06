#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <jni.h>

#include <stdarg.h>

#include "m3/m3.h"
#include "m3/extra/fib32.wasm.h"

#if defined(__arm__)
    #if defined(__ARM_ARCH_7A__)
    #if defined(__ARM_NEON__)
      #if defined(__ARM_PCS_VFP)
        #define ABI "armeabi-v7a/NEON (hard-float)"
      #else
        #define ABI "armeabi-v7a/NEON"
      #endif
    #else
      #if defined(__ARM_PCS_VFP)
        #define ABI "armeabi-v7a (hard-float)"
      #else
        #define ABI "armeabi-v7a"
      #endif
    #endif
  #else
   #define ABI "armeabi"
  #endif
#elif defined(__i386__)
#define ABI "x86"
#elif defined(__x86_64__)
#define ABI "x86_64"
#elif defined(__mips64)  /* mips64el-* toolchain defines __mips__ too */
#define ABI "mips64"
#elif defined(__mips__)
#define ABI "mips"
#elif defined(__aarch64__)
#define ABI "arm64-v8a"
#else
#define ABI "unknown"
#endif

/*
 * Override printf, puts, putchar
 */

static char gBuffer[16*1024] = {};
static char* gBufferPtr = gBuffer;
static char* gBufferEnd = gBuffer+sizeof(gBuffer)-1;

int printf(const char * format, ... )
{
  va_list args;
  va_start (args, format);
  int result = vsnprintf(gBufferPtr, gBufferEnd-gBufferPtr, format, args);
  va_end (args);
  if (result > 0) {
    gBufferPtr += result;
  }
  return result;
}

int puts(const char *s)
{
  return printf("%s\n", s);
}

int putchar(int c)
{
  printf("%c", c);
  return c;
}


#define FATAL(msg, ...) { printf("Fatal: " msg "\n", __VA_ARGS__); return; }

void run_wasm()
{
    M3Result result = c_m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    size_t fsize = fib32_wasm_len-1;

    printf("Loading WebAssembly...\n");

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

    result = m3_FindFunction (&f, env, "fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    printf("Running...\n");

    const char* i_argv[2] = { "40", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("Call: %s", result);

}


JNIEXPORT jstring JNICALL
Java_com_example_hellojni_HelloJni_stringFromJNI( JNIEnv* env,
                                                  jobject thiz )
{
    printf("wasm3 on Android (" ABI ")\n");
    printf("Build " __DATE__ " " __TIME__ "\n");

    clock_t start = clock();
    run_wasm();
    clock_t end = clock();

    printf("Elapsed: %d ms\n", (end - start)*1000 / CLOCKS_PER_SEC);

    return (*env)->NewStringUTF(env, gBuffer);
}
