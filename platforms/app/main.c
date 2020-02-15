//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include "wasm3.h"
#include "m3_api_wasi.h"
#include "m3_api_libc.h"
#include "m3_env.h"

#define FATAL(msg, ...) { printf("Error: [Fatal] " msg "\n", ##__VA_ARGS__); goto _onfatal; }

M3Result repl_load  (IM3Runtime runtime, const char* fn)
{
    M3Result result = m3Err_none;

    u8* wasm = NULL;
    u32 fsize = 0;

    FILE* f = fopen (fn, "rb");
    if (!f) {
        return "cannot open file";
    }
    fseek (f, 0, SEEK_END);
    fsize = ftell(f);
    fseek (f, 0, SEEK_SET);

    if (fsize < 8) {
        return "file is too small";
    } else if (fsize > 10*1024*1024) {
        return "file too big";
    }

    wasm = (u8*) malloc(fsize);
    if (!wasm) {
        return "cannot allocate memory for wasm binary";
    }

    if (fread (wasm, 1, fsize, f) != fsize) {
        return "cannot read file";
    }
    fclose (f);

    IM3Module module;
    result = m3_ParseModule (runtime->environment, &module, wasm, fsize);
    if (result) return result;

    result = m3_LoadModule (runtime, module);
    if (result) return result;

    result = m3_LinkSpecTest (runtime->modules);

    return result;
}

M3Result repl_load_hex  (IM3Runtime runtime, u32 fsize)
{
    M3Result result = m3Err_none;

    if (fsize < 8) {
        return "file is too small";
    } else if (fsize > 10*1024*1024) {
        return "file too big";
    }

    u8* wasm = (u8*) malloc(fsize);
    if (!wasm) {
        return "cannot allocate memory for wasm binary";
    }

    {   // Load hex data from stdin
        u32 wasm_idx = 0;
        char hex[3] = { 0, };
        int hex_idx = 0;
        while (wasm_idx < fsize) {
            int c = fgetc(stdin);
            if (!isxdigit(c)) continue; // Skip non-hex chars
            hex[hex_idx++] = c;
            if (hex_idx == 2) {
                int val = strtol(hex, NULL, 16);
                wasm[wasm_idx++] = val;
                hex_idx = 0;
            }
        }
        fgets(hex, 3, stdin); // Consume a newline
    }

    IM3Module module;
    result = m3_ParseModule (runtime->environment, &module, wasm, fsize);
    if (result) return result;

    result = m3_LoadModule (runtime, module);
    if (result) return result;

    result = m3_LinkSpecTest (runtime->modules);

    return result;
}

M3Result repl_call  (IM3Runtime runtime, const char* name, int argc, const char* argv[])
{
    M3Result result = m3Err_none;

    IM3Function func;
    result = m3_FindFunction (&func, runtime, name);
    if (result) return result;

    // TODO
    if (argc) {
        if (!strcmp(name, "main") || !strcmp(name, "_main")) {
            return "passing arguments to libc main() not implemented";
        }
    }

    result = m3_CallWithArgs (func, argc, argv);
    if (result) return result;

    return result;
}

void repl_free(IM3Runtime* runtime)
{
    if (*runtime) {
        m3_FreeRuntime (*runtime);
    }
}

M3Result repl_init(IM3Environment env, IM3Runtime* runtime)
{
    repl_free(runtime);
    *runtime = m3_NewRuntime (env, 64*1024, NULL);
    if (*runtime == NULL) {
        return "m3_NewRuntime failed";
    }
    return m3Err_none;
}

static
void unescape(char* buff)
{
    char* outp = buff;
    while (*buff) {
        if (*buff == '\\') {
            switch (*(buff+1)) {
            case '0':  *outp++ = '\0'; break;
            case 'b':  *outp++ = '\b'; break;
            case 'n':  *outp++ = '\n'; break;
            case 'r':  *outp++ = '\r'; break;
            case 't':  *outp++ = '\t'; break;
            case 'x': {
                char hex[3] = { *(buff+2), *(buff+3), '\0' };
                *outp++ = strtol(hex, NULL, 16);
                buff += 2;
                break;
            }
            // Otherwise just pass the letter
            // Also handles '\\'
            default: *outp++ = *(buff+1); break;
            }
            buff += 2;
        } else {
            *outp++ = *buff++;
        }
    }
    *outp = '\0';
}

static
int split_argv(char *str, char** argv)
{
    int result = 0;
    char* curr = str;
    int len = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        if (strchr(" \n\r\t", str[i])) {
            if (len) {  // Found space after non-space
                str[i] = '\0';
                //unescape(curr); // TODO: breaks windows build?
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
    puts("Wasm3 v" M3_VERSION " (" __DATE__ " " __TIME__ ", " M3_COMPILER_VER ", " M3_ARCH ")");
}

void print_usage() {
    puts("Usage:");
    puts("  wasm3 <file> [args...]");
    puts("  wasm3 --func <function> <file> [args...]");
    puts("  wasm3 --repl [file]");
}

#define ARGV_SHIFT()  { i_argc--; i_argv++; }
#define ARGV_SET(x)   { if (i_argc > 0) { x = i_argv[0]; ARGV_SHIFT(); } }

int  main  (int i_argc, const char* i_argv[])
{
    M3Result result = m3Err_none;
    
    IM3Environment env = m3_NewEnvironment ();
    IM3Runtime runtime = NULL;
    bool argRepl = false;
    const char* argFile = NULL;
    const char* argFunc = "_start";

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

    if ((argRepl and (i_argc > 1)) or   // repl supports 0 or 1 args
        (not argRepl and (i_argc < 1))  // normal expects at least 1
    ) {
        print_usage();
        return 1;
    }

    ARGV_SET(argFile);

    result = repl_init(env, &runtime);
    if (result) FATAL("repl_init: %s", result);

    if (argFile) {
        result = repl_load(runtime, argFile);
        if (result) FATAL("repl_load: %s", result);

#if defined(d_m3HasWASI)
        result = m3_LinkWASI (runtime->modules);
        if (result) FATAL("m3_LinkWASI: %s", result);
#endif

        result = m3_LinkLibC (runtime->modules);
        if (result) FATAL("m3_LinkLibC: %s", result);

        if (argFunc and not argRepl) {
            if (!strcmp(argFunc, "_start")) {
                // When passing args to WASI, include wasm filename as argv[0]
                result = repl_call(runtime, argFunc, i_argc+1, i_argv-1);
            } else {
                result = repl_call(runtime, argFunc, i_argc, i_argv);
            }
            if (result == m3Err_trapExit) {
                return runtime->exit_code;
            }
            if (result) FATAL("repl_call: %s", result);
        }
    }

    while (argRepl)
    {
        char cmd_buff[1024] = { 0, };
        char* argv[32] = { 0, };
        fprintf(stdout, "wasm3> ");
        fflush(stdout);
        if (!fgets(cmd_buff, sizeof(cmd_buff), stdin)) {
            return 0;
        }
        int argc = split_argv(cmd_buff, argv);
        if (argc <= 0) {
            continue;
        }
        result = m3Err_none;
        if (!strcmp(":init", argv[0])) {
            result = repl_init(env, &runtime);
        } else if (!strcmp(":version", argv[0])) {
            print_version();
        } else if (!strcmp(":exit", argv[0])) {
            repl_free(&runtime);
            return 0;
        } else if (!strcmp(":load", argv[0])) {             // :load <filename>
            result = repl_load(runtime, argv[1]);
        } else if (!strcmp(":load-hex", argv[0])) {         // :load-hex <size>\n <hex-encoded-binary>
            result = repl_load_hex(runtime, atol(argv[1]));
        } else if (argv[0][0] == ':') {
            result = "no such command";
        } else {
            unescape(argv[0]);
            result = repl_call(runtime, argv[0], argc-1, (const char**)(argv+1));
        }

        if (result) {
            fprintf (stderr, "Error: %s", result);
            M3ErrorInfo info;
            m3_GetErrorInfo (runtime, &info);
            fprintf (stderr, " (%s)\n", info.message);
            if (result == m3Err_trapExit) {
                // warn that exit was called
                fprintf(stderr, M3_ARCH "-wasi: exit(%d)\n", runtime->exit_code);
            }
        }
    }

_onfatal:
    if (result) {
        fprintf (stderr, "Error: %s", result);
        if (runtime)
        {
            M3ErrorInfo info;
            m3_GetErrorInfo (runtime, &info);
            fprintf (stderr, " (%s)", info.message);
        }
        fprintf (stderr, "\n");
    }

    m3_FreeRuntime (runtime);
    m3_FreeEnvironment (env);
    
    return 0;
}
