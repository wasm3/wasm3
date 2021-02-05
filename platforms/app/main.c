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
#include "m3_api_tracer.h"
#include "m3_env.h"

#define FATAL(msg, ...) { printf("Error: [Fatal] " msg "\n", ##__VA_ARGS__); goto _onfatal; }

#if defined(d_m3HasWASI) || defined(d_m3HasMetaWASI) || defined(d_m3HasUVWASI)
#define LINK_WASI
#endif

IM3Environment env;
IM3Runtime runtime;


M3Result link_all  (IM3Module module)
{
    M3Result res;
    res = m3_LinkSpecTest (module);
    if (res) return res;

    res = m3_LinkLibC (module);
    if (res) return res;

#if defined(LINK_WASI)
    res = m3_LinkWASI (module);
    if (res) return res;
#endif

#if defined(d_m3HasTracer)
    res = m3_LinkTracer (module);
    if (res) return res;
#endif

    return res;
}


M3Result repl_load  (const char* fn)
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
    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) return result;

    result = m3_LoadModule (runtime, module);
    if (result) return result;

    result = link_all (module);

    return result;
}

M3Result repl_load_hex  (u32 fsize)
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
        if (!fgets(hex, 3, stdin)) // Consume a newline
            return "cannot read EOL";
    }

    IM3Module module;
    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) return result;

    result = m3_LoadModule (runtime, module);
    if (result) return result;

    result = link_all (module);

    return result;
}

M3Result repl_call  (const char* name, int argc, const char* argv[])
{
    IM3Function func;
    M3Result result = m3_FindFunction (&func, runtime, name);
    if (result) return result;

    if (argc && (!strcmp(name, "main") || !strcmp(name, "_main"))) {
        return "passing arguments to libc main() not implemented";
    }

    if (!strcmp(name, "_start")) {
#if defined(LINK_WASI)
        m3_wasi_context_t* wasi_ctx = m3_GetWasiContext();
        wasi_ctx->argc = argc;
        wasi_ctx->argv = argv;

        IM3Function func;
        M3Result result = m3_FindFunction (&func, runtime, "_start");
        if (result) return result;

        result = m3_CallWithArgs(func, 0, NULL);

        if (result == m3Err_trapExit) {
            exit(wasi_ctx->exit_code);
        }

        return result;
#else
        return "WASI not linked";
#endif
    }

    int arg_count = m3_GetArgCount(func);
    if (argc < arg_count) {
        return "not enough arguments";
    } else if (argc > arg_count) {
        return "too many arguments";
    }

    result = m3_CallWithArgs (func, argc, argv);
    if (result) return result;

    // TODO: Stack access API
    uint64_t* stack = (uint64_t*)runtime->stack;

    int ret_count = m3_GetRetCount(func);
    if (ret_count <= 0) {
        fprintf (stderr, "Result: <Empty Stack>\n");
    }
    for (int i = 0; i < ret_count; i++) {
        switch (m3_GetRetType(func, i)) {
        case c_m3Type_i32:  fprintf (stderr, "Result: %" PRIi32 "\n", *(i32*)(stack));  break;
        case c_m3Type_i64:  fprintf (stderr, "Result: %" PRIi64 "\n", *(i64*)(stack));  break;
        case c_m3Type_f32:  fprintf (stderr, "Result: %f\n",   *(f32*)(stack));  break;
        case c_m3Type_f64:  fprintf (stderr, "Result: %lf\n",  *(f64*)(stack));  break;
        default: return "unknown return type";
        }
        stack++;
    }

    return result;
}

// :invoke is used by spec tests, so it treats floats as raw data
M3Result repl_invoke  (const char* name, int argc, const char* argv[])
{
    IM3Function func;
    M3Result result = m3_FindFunction (&func, runtime, name);
    if (result) return result;

    int arg_count = m3_GetArgCount(func);
    if (argc > 128) {
        return "arguments limit reached";
    } else if (argc < arg_count) {
        return "not enough arguments";
    } else if (argc > arg_count) {
        return "too many arguments";
    }

    static uint64_t    argbuff[128];
    static const void* argptrs[128];
    memset(argbuff, 0, sizeof(argbuff));
    memset(argptrs, 0, sizeof(argptrs));

    for (int i = 0; i < argc; i++) {
        u64* s = &argbuff[i];
        argptrs[i] = s;
        switch (m3_GetArgType(func, i)) {
        case c_m3Type_i32:
        case c_m3Type_f32:  *(u32*)(s) = strtoul(argv[i], NULL, 10);  break;
        case c_m3Type_i64:
        case c_m3Type_f64:  *(u64*)(s) = strtoull(argv[i], NULL, 10); break;
        default: return "unknown argument type";
        }
    }

    result = m3_Call (func, argc, argptrs);
    if (result) return result;

    // TODO: Stack access API
    uint64_t* stack = (uint64_t*)runtime->stack;

    unsigned ret_count = m3_GetRetCount(func);
    if (ret_count <= 0) {
        fprintf (stderr, "Result: <Empty Stack>\n");
    }
    for (unsigned i = 0; i < ret_count; i++) {
        switch (m3_GetRetType(func, i)) {
        case c_m3Type_i32:
        case c_m3Type_f32:
            fprintf (stderr, "Result: %" PRIu32 "\n", *(u32*)(stack));  break;
        case c_m3Type_i64:
        case c_m3Type_f64:
            fprintf (stderr, "Result: %" PRIu64 "\n", *(u64*)(stack));  break;
        default: return "unknown return type";
        }
        stack++;
    }

    return result;
}

M3Result repl_dump()
{
    uint32_t len;
    uint8_t* mem = m3_GetMemory(runtime, &len, 0);
    if (mem) {
        FILE* f = fopen ("wasm3_dump.bin", "wb");
        if (!f) {
            return "cannot open file";
        }
        if (fwrite (mem, 1, len, f) != len) {
            return "cannot write file";
        }
        fclose (f);
    }
    return m3Err_none;
}

void repl_free()
{
    if (runtime) {
        m3_FreeRuntime (runtime);
        runtime = NULL;
    }
}

M3Result repl_init(unsigned stack)
{
    repl_free();
    runtime = m3_NewRuntime (env, stack, NULL);
    if (runtime == NULL) {
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
                *outp = strtol(hex, NULL, 16);
                buff += 2; outp += 1;
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
    const char* wasm3_env = getenv("WASM3");
    const char* wasm3_arch = getenv("WASM3_ARCH");

    printf("Wasm3 v" M3_VERSION "%s on %s\n",
            (wasm3_arch || wasm3_env) ? " self-hosting" : "",
            wasm3_arch ? wasm3_arch : M3_ARCH);

    printf("Build: " __DATE__ " " __TIME__ ", " M3_COMPILER_VER "\n");
}

void print_usage() {
    puts("Usage:");
    puts("  wasm3 [options] <file> [args...]");
    puts("  wasm3 --repl [file]");
    puts("Options:");
    puts("  --func <function>     function to run       default: _start");
    puts("  --stack-size <size>   stack size in bytes   default: 64KB");
    puts("  --dump-on-trap        dump wasm memory");
}

#define ARGV_SHIFT()  { i_argc--; i_argv++; }
#define ARGV_SET(x)   { if (i_argc > 0) { x = i_argv[0]; ARGV_SHIFT(); } }

int  main  (int i_argc, const char* i_argv[])
{
    M3Result result = m3Err_none;
    env = m3_NewEnvironment ();
    runtime = NULL;

    bool argRepl = false;
    bool argDumpOnTrap = false;
    const char* argFile = NULL;
    const char* argFunc = "_start";
    unsigned argStackSize = 64*1024;

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
        } else if (!strcmp("--dump-on-trap", arg)) {
            argDumpOnTrap = true;
        } else if (!strcmp("--stack-size", arg)) {
            const char* tmp = "65536";
            ARGV_SET(tmp);
            argStackSize = atol(tmp);
        } else if (!strcmp("--dir", arg)) {
            const char* argDir;
            ARGV_SET(argDir);
            (void)argDir;
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

    result = repl_init(argStackSize);
    if (result) FATAL("repl_init: %s", result);

    if (argFile) {
        result = repl_load(argFile);
        if (result) FATAL("repl_load: %s", result);

        if (argFunc and not argRepl) {
            if (!strcmp(argFunc, "_start")) {
                // When passing args to WASI, include wasm filename as argv[0]
                result = repl_call(argFunc, i_argc+1, i_argv-1);
            } else {
                result = repl_call(argFunc, i_argc, i_argv);
            }

            if (result) {
                if (argDumpOnTrap) {
                    repl_dump();
                }
                FATAL("repl_call: %s", result);
            }
        }
    }

    while (argRepl)
    {
        char cmd_buff[2048] = { 0, };
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
            result = repl_init(argStackSize);
        } else if (!strcmp(":version", argv[0])) {
            print_version();
        } else if (!strcmp(":exit", argv[0])) {
            repl_free();
            return 0;
        } else if (!strcmp(":load", argv[0])) {             // :load <filename>
            result = repl_load(argv[1]);
        } else if (!strcmp(":load-hex", argv[0])) {         // :load-hex <size>\n <hex-encoded-binary>
            result = repl_load_hex(atol(argv[1]));
        } else if (!strcmp(":dump", argv[0])) {
            result = repl_dump();
        } else if (!strcmp(":invoke", argv[0])) {
            unescape(argv[1]);
            result = repl_invoke(argv[1], argc-2, (const char**)(argv+2));
        } else if (argv[0][0] == ':') {
            result = "no such command";
        } else {
            unescape(argv[0]);
            result = repl_call(argv[0], argc-1, (const char**)(argv+1));
        }

        if (result) {
            fprintf (stderr, "Error: %s", result);
            M3ErrorInfo info;
            m3_GetErrorInfo (runtime, &info);
            fprintf (stderr, " (%s)\n", info.message);
            //TODO: if (result == m3Err_trapExit) {
                // warn that exit was called
            //    fprintf(stderr, M3_ARCH "-wasi: exit(%d)\n", runtime->exit_code);
            //}
        }
    }

_onfatal:
    if (result) {
        fprintf (stderr, "Error: %s", result);
        if (runtime)
        {
            M3ErrorInfo info;
            m3_GetErrorInfo (runtime, &info);
            if (strlen(info.message)) {
                fprintf (stderr, " (%s)", info.message);
            }
        }
        fprintf (stderr, "\n");
    }

    m3_FreeRuntime (runtime);
    m3_FreeEnvironment (env);

    return result ? 1 : 0;
}
