#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "m3.h"
#include "m3_api_wasi.h"
#include "m3_api_libc.h"
#include "m3_env.h"

#define FATAL(msg, ...) { printf("Error: [Fatal] " msg "\n", ##__VA_ARGS__); goto _onfatal; }

M3Result repl_load  (IM3Runtime env, const char* fn)
{
    M3Result result = c_m3Err_none;

    u8* wasm = NULL;
    u32 fsize = 0;

    FILE* f = fopen (fn, "rb");
    if (!f) {
        return "cannot open file";
    }
    fseek (f, 0, SEEK_END);
    fsize = ftell(f);
    fseek (f, 0, SEEK_SET);

    if (fsize > 10*1024*1024) {
        return "file too big";
    }

    wasm = (u8*) malloc(fsize);
    fread (wasm, 1, fsize, f);
    fclose (f);

    IM3Module module;
    result = m3_ParseModule (&module, wasm, fsize);
    if (result) return result;

    result = m3_LoadModule (env, module);
    if (result) return result;

    return result;
}

M3Result repl_call  (IM3Runtime env, const char* name, int argc, const char* argv[])
{
    // TODO
    if (argc) {
        if (!strcmp(name, "main") || !strcmp(name, "_main")) {
            return "passing arguments to libc main() not implemented";
        } else if (!strcmp(name, "_start")) {
            return "passing arguments to wasi _start() not implemented";
        }
    }

    M3Result result = c_m3Err_none;

    IM3Function func;
    result = m3_FindFunction (&func, env, name);
    if (result) return result;

    result = m3_CallWithArgs (func, argc, argv);
    if (result) return result;

    return result;
}

void repl_free(IM3Runtime* env) {
    if (*env) {
        m3_FreeRuntime (*env);
    }
}

M3Result repl_init(IM3Runtime* env) {
    repl_free(env);
    *env = m3_NewRuntime (8*1024);
    if (*env == NULL) {
        return "m3_NewRuntime failed";
    }
    return c_m3Err_none;
}

int split_argv(char *str, const char** argv)
{
    int result = 0;
    char* curr = str;
    int len = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        if (strchr(" \n\r\t", str[i])) {
            if (len) {  // Found space after non-space
                str[i] = '\0';
                argv[result++] = curr;
                len = 0;
            }
        } else {
            if (!len) { // Found non-space after space
                curr = &str[i];
            }
            len++;
        }
    }
    argv[result] = NULL;
    return result;
}

void print_version() {
    printf("wasm3 v0.4.0\n");
}

void print_usage() {
    printf("Usage:\n");
    printf("  wasm3 <file> [args...]\n");
    printf("  wasm3 --func <function> <file> [args...]\n");
    printf("Repl usage:\n");
    printf("  wasm3 --repl [file] [function] [args...]\n");
    printf("  wasm3 --repl --func <function> <file> [args...]\n");
}

#define ARGV_SHIFT()  { i_argc--; i_argv++; }
#define ARGV_SET(x)   { if (i_argc > 0) { x = i_argv[0]; ARGV_SHIFT(); } }

int  main  (int i_argc, const char* i_argv[])
{
    M3Result result = c_m3Err_none;
    IM3Runtime env = NULL;
    bool argRepl = false;
    const char* argFile = NULL;
    const char* argFunc = NULL;

//    m3_PrintM3Info ();

    ARGV_SHIFT(); // Skip executable name

    while (i_argc > 0)
    {
        const char* arg = i_argv[0];
        if (arg[0] != '-') break;

        ARGV_SHIFT();
        if (!strcmp("--help", arg) or !strcmp("-h", arg)) {
            print_usage();
            return 0;
        } else if (!strcmp("--version", arg)) {
            print_version();
            return 0;
        } else if (!strcmp("--repl", arg)) {
            argRepl = true;
        } else if (!strcmp("--func", arg) or !strcmp("-f", arg)) {
            ARGV_SET(argFunc);
        }
    }

    if (argRepl) {
        ARGV_SET(argFile);
        if (!argFunc) ARGV_SET(argFunc);
    } else {
        if (i_argc < 1) {
            print_usage();
            return 1;
        }
        ARGV_SET(argFile);
        if (!argFunc) ARGV_SET(argFunc); //argFunc = "_start"; //TODO: reverted for now
    }

    //printf("=== argFile: %s, argFunc: %s, args: %d\n", argFile, argFunc, i_argc);

    result = repl_init(&env);
    if (result) FATAL("repl_init: %s", result);

    if (argFile) {
        result = repl_load(env, argFile);
        if (result) FATAL("repl_load: %s", result);

        result = m3_LinkWASI (env->modules);
        if (result) FATAL("m3_LinkWASI: %s", result);

        result = m3_LinkLibC (env->modules);
        if (result) FATAL("m3_LinkLibC: %s", result);

        if (argFunc) {
            result = repl_call(env, argFunc, i_argc, i_argv);
            if (result) FATAL("repl_call: %s", result);
        }
    }

    while (argRepl)
    {
        char cmd_buff[128] = {};
        const char* argv[32] = {};
        fprintf(stdout, "> ");
        fflush(stdout);
        if (!fgets(cmd_buff, sizeof(cmd_buff), stdin)) {
            return 0;
        }
        int argc = split_argv(cmd_buff, argv);
        if (argc <= 0) {
            continue;
        }
        M3Result result = c_m3Err_none;
        if (!strcmp("init", argv[0])) {
            result = repl_init(&env);
        } else if (!strcmp("exit", argv[0])) {
            repl_free(&env);
            return 0;
        } else if (!strcmp("load", argv[0])) {
            result = repl_load(env, argv[1]);
        } else if (!strcmp("call", argv[0])) {
            result = repl_call(env, argv[1], argc-2, argv+2);
        } else {
            result = "no such command";
        }

        if (result) {
            printf ("Error: %s", result);
            M3ErrorInfo info = m3_GetErrorInfo (env);
            if (strlen(info.message)) {
                printf (" (%s)\n", info.message);
            } else {
                printf ("\n");
            }
        }
    }

_onfatal:
    if (result) {
        printf ("Error: %s", result);
        if (env)
        {
            M3ErrorInfo info = m3_GetErrorInfo (env);
            printf (" (%s)", info.message);
        }
    }

    printf ("\n");

    return 0;
}
