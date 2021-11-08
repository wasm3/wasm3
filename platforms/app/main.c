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
#include "m3_api_libc.h"

#if defined(d_m3HasWASI) || defined(d_m3HasMetaWASI) || defined(d_m3HasUVWASI)
#include "m3_api_wasi.h"
#define LINK_WASI
#endif

#if defined(d_m3HasTracer)
#include "m3_api_tracer.h"
#endif

// TODO: remove
#include "m3_env.h"

/*
 * NOTE: Gas metering/limit only applies to pre-instrumented modules.
 * You can generate a metered version from any wasm file automatically, using
 *   https://github.com/ewasm/wasm-metering
 */
#define GAS_LIMIT       500000000
#define GAS_FACTOR      10000LL

#define MAX_MODULES     16

#define FATAL(msg, ...) { fprintf(stderr, "Error: [Fatal] " msg "\n", ##__VA_ARGS__); goto _onfatal; }


static IM3Environment env;
static IM3Runtime runtime;

static u8* wasm_bins[MAX_MODULES];
static int wasm_bins_qty = 0;

#if defined(GAS_LIMIT)

static int64_t initial_gas = GAS_FACTOR * GAS_LIMIT;
static int64_t current_gas = GAS_FACTOR * GAS_LIMIT;
static bool is_gas_metered = false;

m3ApiRawFunction(metering_usegas)
{
    m3ApiGetArg     (int32_t, gas)

    current_gas -= gas;

    if (M3_UNLIKELY(current_gas < 0)) {
        m3ApiTrap("[trap] Out of gas");
    }
    m3ApiSuccess();
}

#endif // GAS_LIMIT


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

#if defined(GAS_LIMIT)
    res = m3_LinkRawFunction (module, "metering", "usegas", "v(i)", &metering_usegas);
    if (!res) {
        fprintf(stderr, "Warning: Gas is limited to %0.4f\n", (double)(current_gas) / GAS_FACTOR);
        is_gas_metered = true;
    }
    if (res == m3Err_functionLookupFailed) { res = NULL; }
#endif

    return res;
}

const char* modname_from_fn(const char* fn)
{
    const char* sep = "/\\:*?";
    char c;
    while ((c = *sep++)) {
        const char* off = strrchr(fn, c) + 1;
        fn = (fn < off) ? off : fn;
    }
    return fn;
}

M3Result repl_load  (const char* fn)
{
    M3Result result = m3Err_none;
    IM3Module module = NULL;

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
        result = "file is too small";
        goto on_error;
    } else if (fsize > 64*1024*1024) {
        result = "file is too big";
        goto on_error;
    }

    wasm = (u8*) malloc(fsize);
    if (!wasm) {
        result = "cannot allocate memory for wasm binary";
        goto on_error;
    }

    if (fread (wasm, 1, fsize, f) != fsize) {
        result = "cannot read file";
        goto on_error;
    }
    fclose (f);
    f = NULL;

    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) goto on_error;

    result = m3_LoadModule (runtime, module);
    if (result) goto on_error;

    m3_SetModuleName(module, modname_from_fn(fn));

    result = link_all (module);
    if (result) goto on_error;

    if (wasm_bins_qty < MAX_MODULES) {
        wasm_bins[wasm_bins_qty++] = wasm;
    }

    return result;

on_error:
    m3_FreeModule(module);
    if (wasm) free(wasm);
    if (f) fclose(f);

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

void print_gas_used()
{
#if defined(GAS_LIMIT)
    if (is_gas_metered) {
        fprintf(stderr, "Gas used: %0.4f\n", (double)(initial_gas - current_gas) / GAS_FACTOR);
    }
#endif
}

void print_backtrace()
{
    IM3BacktraceInfo info = m3_GetBacktrace(runtime);
    if (!info) {
        return;
    }

    fprintf(stderr, "==== wasm backtrace:");

    int frameCount = 0;
    IM3BacktraceFrame curr = info->frames;
    while (curr)
    {
        fprintf(stderr, "\n  %d: 0x%06x - %s!%s",
                           frameCount, curr->moduleOffset,
                           m3_GetModuleName (m3_GetFunctionModule(curr->function)),
                           m3_GetFunctionName (curr->function)
               );
        curr = curr->next;
        frameCount++;
    }
    if (info->lastFrame == M3_BACKTRACE_TRUNCATED) {
        fprintf(stderr, "\n  (truncated)");
    }
    fprintf(stderr, "\n");
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
        // Strip wasm file path
        if (argc > 0) {
            argv[0] = modname_from_fn(argv[0]);
        }

        m3_wasi_context_t* wasi_ctx = m3_GetWasiContext();
        wasi_ctx->argc = argc;
        wasi_ctx->argv = argv;

        result = m3_CallArgv(func, 0, NULL);

        print_gas_used();

        if (result == m3Err_trapExit) {
            exit(wasi_ctx->exit_code);
        }

        return result;
#else
        return "WASI not linked";
#endif
    }

    int arg_count = m3_GetArgCount(func);
    int ret_count = m3_GetRetCount(func);
    if (argc < arg_count) {
        return "not enough arguments";
    } else if (argc > arg_count) {
        return "too many arguments";
    }

    result = m3_CallArgv (func, argc, argv);

    print_gas_used();

    if (result) return result;

    static uint64_t    valbuff[128];
    static const void* valptrs[128];
    memset(valbuff, 0, sizeof(valbuff));
    for (int i = 0; i < ret_count; i++) {
        valptrs[i] = &valbuff[i];
    }
    result = m3_GetResults (func, ret_count, valptrs);
    if (result) return result;

    if (ret_count <= 0) {
        fprintf (stderr, "Result: <Empty Stack>\n");
    }
    for (int i = 0; i < ret_count; i++) {
        switch (m3_GetRetType(func, i)) {
        case c_m3Type_i32:  fprintf (stderr, "Result: %" PRIi32 "\n", *(i32*)valptrs[i]);  break;
        case c_m3Type_i64:  fprintf (stderr, "Result: %" PRIi64 "\n", *(i64*)valptrs[i]);  break;
# if d_m3HasFloat
        case c_m3Type_f32:  fprintf (stderr, "Result: %" PRIf32 "\n", *(f32*)valptrs[i]);  break;
        case c_m3Type_f64:  fprintf (stderr, "Result: %" PRIf64 "\n", *(f64*)valptrs[i]);  break;
# endif
        default: return "unknown return type";
        }
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
    int ret_count = m3_GetRetCount(func);

    if (argc > 128) {
        return "arguments limit reached";
    } else if (argc < arg_count) {
        return "not enough arguments";
    } else if (argc > arg_count) {
        return "too many arguments";
    }

    static uint64_t    valbuff[128];
    static const void* valptrs[128];
    memset(valbuff, 0, sizeof(valbuff));
    memset(valptrs, 0, sizeof(valptrs));

    for (int i = 0; i < argc; i++) {
        u64* s = &valbuff[i];
        valptrs[i] = s;
        switch (m3_GetArgType(func, i)) {
        case c_m3Type_i32:
        case c_m3Type_f32:  *(u32*)(s) = strtoul(argv[i], NULL, 10);  break;
        case c_m3Type_i64:
        case c_m3Type_f64:  *(u64*)(s) = strtoull(argv[i], NULL, 10); break;
        default: return "unknown argument type";
        }
    }

    result = m3_Call (func, argc, valptrs);
    if (result) return result;

    // reuse valbuff for return values
    memset(valbuff, 0, sizeof(valbuff));
    for (int i = 0; i < ret_count; i++) {
        valptrs[i] = &valbuff[i];
    }
    result = m3_GetResults (func, ret_count, valptrs);
    if (result) return result;

    fprintf (stderr, "Result: ");
    if (ret_count <= 0) {
        fprintf (stderr, "<Empty Stack>");
    }
    for (int i = 0; i < ret_count; i++) {
        switch (m3_GetRetType(func, i)) {
        case c_m3Type_i32: fprintf (stderr, "%" PRIu32 ":i32", *(u32*)valptrs[i]);  break;
        case c_m3Type_f32: fprintf (stderr, "%" PRIu32 ":f32", *(u32*)valptrs[i]);  break;
        case c_m3Type_i64: fprintf (stderr, "%" PRIu64 ":i64", *(u64*)valptrs[i]);  break;
        case c_m3Type_f64: fprintf (stderr, "%" PRIu64 ":f64", *(u64*)valptrs[i]);  break;
        default: return "unknown return type";
        }
        if (i != ret_count-1) {
            fprintf (stderr, ", ");
        }
    }
    fprintf (stderr, "\n");

    return result;
}

M3Result repl_global_get  (const char* name)
{
    IM3Global g = m3_FindGlobal(runtime->modules, name);

    M3TaggedValue tagged;
    M3Result err = m3_GetGlobal (g, &tagged);
    if (err) return err;

    switch (tagged.type) {
    case c_m3Type_i32:  fprintf (stderr, "%" PRIu32 ":i32\n", tagged.value.i32);  break;
    case c_m3Type_i64:  fprintf (stderr, "%" PRIu64 ":i64\n", tagged.value.i64);  break;
    case c_m3Type_f32:  fprintf (stderr, "%" PRIf32 ":f32\n", tagged.value.f32);  break;
    case c_m3Type_f64:  fprintf (stderr, "%" PRIf64 ":f64\n", tagged.value.f64);  break;
    default:            return m3Err_invalidTypeId;
    }
    return m3Err_none;
}

M3Result repl_global_set  (const char* name, const char* value)
{
    IM3Global g = m3_FindGlobal(runtime->modules, name);

    M3TaggedValue tagged = {
        .type      = m3_GetGlobalType(g)
    };

    switch (tagged.type) {
    case c_m3Type_i32:  tagged.value.i32 = strtoul(value, NULL, 10);    break;
    case c_m3Type_i64:  tagged.value.i64 = strtoull(value, NULL, 10);   break;
    case c_m3Type_f32:  tagged.value.f32 = strtod(value, NULL);         break;
    case c_m3Type_f64:  tagged.value.f64 = strtod(value, NULL);         break;
    default:            return m3Err_invalidTypeId;
    }

    return m3_SetGlobal (g, &tagged);
}

M3Result repl_compile  ()
{
    return m3_CompileModule(runtime->modules);
}

M3Result repl_dump  ()
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

void repl_free  ()
{
    if (runtime) {
        m3_FreeRuntime (runtime);
        runtime = NULL;
    }

    for (int i = 0; i < wasm_bins_qty; i++) {
        free (wasm_bins[i]);
        wasm_bins[i] = NULL;
    }
}

M3Result repl_init  (unsigned stack)
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
    puts("  --compile             disable lazy compilation");
    puts("  --dump-on-trap        dump wasm memory");
    puts("  --gas-limit           set gas limit");
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
    bool argCompile = false;
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
        } else if (!strcmp("--compile", arg)) {
            argCompile = true;
        } else if (!strcmp("--stack-size", arg)) {
            const char* tmp = "65536";
            ARGV_SET(tmp);
            argStackSize = atol(tmp);
        } else if (!strcmp("--gas-limit", arg)) {
            const char* tmp = "0";
            ARGV_SET(tmp);
            initial_gas = current_gas = GAS_FACTOR * atol(tmp);
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

        if (argCompile) {
            repl_compile();
        }

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
                print_backtrace();
                goto _onfatal;
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
        } else if (!strcmp(":get-global", argv[0])) {
            result = repl_global_get(argv[1]);
        } else if (!strcmp(":set-global", argv[0])) {
            result = repl_global_set(argv[1], argv[2]);
        } else if (!strcmp(":dump", argv[0])) {
            result = repl_dump();
        } else if (!strcmp(":compile", argv[0])) {
            result = repl_compile();
        } else if (!strcmp(":invoke", argv[0])) {
            unescape(argv[1]);
            result = repl_invoke(argv[1], argc-2, (const char**)(argv+2));
        } else if (argv[0][0] == ':') {
            result = "no such command";
        } else {
            unescape(argv[0]);
            result = repl_call(argv[0], argc-1, (const char**)(argv+1));
            if (result) {
                print_backtrace();
            }
        }

        if (result == m3Err_trapExit) {
            //TODO: fprintf(stderr, M3_ARCH "-wasi: exit(%d)\n", runtime->exit_code);
        } else if (result) {
            fprintf (stderr, "Error: %s", result);
            M3ErrorInfo info;
            m3_GetErrorInfo (runtime, &info);
            fprintf (stderr, " (%s)\n", info.message);
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
