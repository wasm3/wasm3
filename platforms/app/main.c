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
#include <errno.h>

#include "wasm3.h"
#include "m3_config.h"     // the build options the version banner reports
#include "m3_host.h"       // mapping a module rather than reading it
#include "m3_api_libc.h"

#if defined(d_m3HasWASI) || defined(d_m3HasMetaWASI) || defined(d_m3HasUVWASI)
#  include "m3_api_wasi.h"
#  define LINK_WASI
#endif

#if defined(d_m3HasTracer)
#  include "m3_api_tracer.h"
#endif

#include "spectest.wasm.h"

// What --gas-meter budgets when --gas-limit doesn't say.
#define GAS_LIMIT       1000000000000000.0

#define MAX_MODULES     64

#define FATAL(msg, ...) { fprintf(stderr, "Error: [Fatal] " msg "\n", ##__VA_ARGS__); goto _onfatal; }

#if defined(_MSC_VER)

#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

static inline
bool launched_from_gui_shell ()
{
    /* When Explorer directly starts a console executable, Windows
     * normally creates a new console containing only that executable.
     * When invoked from cmd.exe / PowerShell, the shell is normally
     * attached to the same console as well. */
    DWORD processes[2];
    return 1 == GetConsoleProcessList(processes, 2);
}
#else
static inline
bool launched_from_gui_shell ()
{
    return false;
}
#endif


static IM3Environment env;
static IM3Runtime     runtime;

// Every wasm binary handed to the runtime. A module points into its bytes, and
// m3_LoadModule takes ownership of the module whether or not it succeeds, so
// the bytes have to live until the runtime is freed. One spec-test file can
// hand over hundreds, so this grows instead of being capped.
static M3HostFile* wasm_bins     = NULL;
static int         wasm_bins_qty = 0;
static int         wasm_bins_cap = 0;

// Takes ownership of i_bin's bytes on success; the caller still owns them on failure.
static
bool keep_wasm_bin (const M3HostFile* i_bin)
{
    if (wasm_bins_qty == wasm_bins_cap) {
        int         cap   = wasm_bins_cap ? wasm_bins_cap * 2 : 16;
        M3HostFile* grown = (M3HostFile*)realloc(wasm_bins, (size_t)cap * sizeof(M3HostFile));
        if (!grown) {
            return false;
        }
        wasm_bins     = grown;
        wasm_bins_cap = cap;
    }

    wasm_bins[wasm_bins_qty++] = *i_bin;
    return true;
}

// The largest module the app will take on
#define MAX_WASM_SIZE   (256 * 1024 * 1024)

// The module's bytes, mapped where the system will and read where it will not - see
// m3_HostMapFile. On success the caller owns them and gives them back with
// m3_HostUnmapFile, whichever way they arrived.
static
M3Result read_wasm_file (const char* i_path, M3HostFile* o_bin)
{
    if (not m3_HostMapFile(i_path, MAX_WASM_SIZE, o_bin)) {
        return "cannot open file";
    }

    if (o_bin->size < 8) {
        m3_HostUnmapFile(o_bin);
        return "file is too small";
    }

    return m3Err_none;
}

// the module the most recent :load / :load-hex produced
static IM3Module lastLoadedModule = NULL;

static bool        argGasMeter     = false;
static double      argGasLimit     = GAS_LIMIT;
static const char* argSnapshotFile = NULL;
static const char* argSnapshotName = NULL;
static const char* argResumeFile   = NULL;
static bool        argResumeNone   = false;
static bool        argInterrupt    = false;
// A module that carries a snapshot resumes it rather than starting cold. Only
// the one-shot run does that, so the repl's :load is left alone.
static bool        argAutoResume     = false;
static bool        argResumeEmbedded = false;
static const char* argFunc           = "_start";
static char        argSnapshotFileBuf[1024];
static char        argResumeFileBuf[1024];
static char        argFileBuf[1024];

// Takes the <name> off a "<path>.wasm:<name>" argument. --snapshot and
// --resume name the snapshot they act on; the file to run may name one too,
// and an option wins over it, so `wasm3 app.wasm:a --resume app.wasm:b` resumes b.
static
void set_snapshot_name (const char* i_name, bool i_fromOption)
{
    static bool named_by_option = false;

    if (not i_name or (named_by_option and not i_fromOption)) {
        return;
    }

    argSnapshotName = i_name;
    named_by_option = named_by_option or i_fromOption;
}

static
void parse_wasm_path (const char* i_arg, char* o_path, size_t i_size, const char** o_name)
{
    // the last ".wasm:" in the argument, so a path that holds one earlier -
    // a directory called "x.wasm:y" - still splits where the name starts
    const char* p    = NULL;
    const char* scan = i_arg;

    while ((scan = strstr(scan, ".wasm:")) != NULL) {
        p = scan;
        scan += 1;
    }

    if (p) {
        size_t len = (size_t)(p - i_arg + 5);
        if (len >= i_size) {
            len = i_size - 1;
        }
        memcpy(o_path, i_arg, len);
        o_path[len] = '\0';
        *o_name     = p + 6;
    } else {
        snprintf(o_path, i_size, "%s", i_arg);
        *o_name = NULL;
    }
}

M3Result link_all (IM3Module module)
{
    M3Result res;
    res = m3_LinkLibC(module);
    if (res) {
        return res;
    }

#if defined(LINK_WASI)
    res = m3_LinkWASI(module);
    if (res) {
        return res;
    }
#endif

#if defined(d_m3HasTracer)
    res = m3_LinkTracer(module);
    if (res) {
        return res;
    }
#endif

    return res;
}

const char* modname_from_fn (const char* fn)
{
    const char* sep = "/\\:*?";

    char c;
    while ((c = *sep++)) {
        const char* off = strrchr(fn, c);

        fn = (off && fn < off + 1) ? off + 1 : fn;
    }
    return fn;
}

M3Result repl_load (const char* fn)
{
    IM3Module  module = NULL;
    M3HostFile bin;

    M3Result result = read_wasm_file(fn, &bin);
    if (result) {
        return result;
    }

    result = m3_ParseModule(env, &module, (const u8*)bin.data, (u32)bin.size);
    if (result) {
        goto on_error;
    }

#if d_m3HasSnapshots
    // before the module is loaded: what the snapshot names has to compile with
    // suspension already on
    if (argAutoResume and m3_HasSnapshot(module, argSnapshotName)) {
        argResumeEmbedded = true;
        m3_SetSuspendable(runtime, true);
    }
#endif

    // The module points into the binary, and m3_LoadModule takes ownership of
    // the module whether or not it succeeds, so the bytes have to outlive this
    // call either way. Hand them over before loading rather than after.
    if (not keep_wasm_bin(&bin)) {
        result = "cannot allocate memory for wasm binary";
        goto on_error;
    }
    memset(&bin, 0, sizeof(bin));       // the list owns them now

    result = m3_LoadModule(runtime, module);
    if (result) {
        goto on_error_after_load;
    }

    m3_SetModuleName(module, modname_from_fn(fn));
    lastLoadedModule = module;

    result = link_all(module);
    if (result) {
        goto on_error_after_load;
    }

    // a snapshot about to be restored already holds what the start function
    // did, and restoring marks it done
    if (not argResumeFile and not argResumeEmbedded) {
        result = m3_RunStart(module);
        if (result) {
            goto on_error_after_load;
        }
    }

    return result;

on_error:
    m3_FreeModule(module);          // never handed to the runtime

on_error_after_load:
    m3_HostUnmapFile(&bin);

    return result;
}

M3Result repl_load_hex (u32 fsize)
{
    M3Result  result = m3Err_none;
    IM3Module module = NULL;

    u8* wasm = NULL;

    if (fsize < 8) {
        result = "file is too small";
    } else if (fsize > 10 * 1024 * 1024) {
        result = "file too big";
    } else {
        wasm = (u8*)m3_Malloc("Wasm Binary", fsize);
        if (!wasm) {
            result = "cannot allocate memory for wasm binary";
        }
    }

    {   // Load hex data from stdin.
        // The payload is consumed even when the size was rejected above:
        // whatever is left behind gets read back as the next repl command.
        u32  wasm_idx = 0;
        char hex[3]   = { 0, 0, 0 };
        int  hex_idx  = 0;
        while (wasm_idx < fsize) {
            int c = fgetc(stdin);
            if (c == EOF) {
                m3_Free(wasm);
                return "unexpected end of input";
            }
            if (!isxdigit(c)) {
                continue; // Skip non-hex chars
            }
            hex[hex_idx++] = c;
            if (hex_idx == 2) {
                int val = strtol(hex, NULL, 16);
                if (wasm) {
                    wasm[wasm_idx] = val;
                }
                wasm_idx++;
                hex_idx = 0;
            }
        }
        int c;                          // Consume the rest of the line
        while ((c = fgetc(stdin)) != EOF && c != '\n') {
        }
    }

    if (result) {
        return result;
    }

    result = m3_ParseModule(env, &module, wasm, fsize);
    if (result) {
        m3_Free(wasm);
        return result;
    }

#if d_m3HasSnapshots
    if (argAutoResume and m3_HasSnapshot(module, argSnapshotName)) {
        argResumeEmbedded = true;
        m3_SetSuspendable(runtime, true);
    }
#endif

    // see the note in repl_load: the runtime owns the module from here on, so the
    // binary it points into has to be handed over first. Nothing was mapped here -
    // these bytes arrived over stdin - so this is the shape m3_HostUnmapFile frees.
    M3HostFile bin;
    memset(&bin, 0, sizeof(bin));
    bin.handle = d_m3HostNoHandle;
    bin.data   = wasm;
    bin.size   = fsize;

    if (not keep_wasm_bin(&bin)) {
        m3_FreeModule(module);
        m3_Free(wasm);
        return "cannot allocate memory for wasm binary";
    }

    result = m3_LoadModule(runtime, module);
    if (result) {
        return result;
    }

    lastLoadedModule = module;

    result = link_all(module);
    if (result) {
        return result;
    }

    // a snapshot about to be restored already holds what the start function
    // did, and restoring marks it done
    if (not argResumeFile and not argResumeEmbedded) {
        return m3_RunStart(module);
    }

    return m3Err_none;
}

void print_gas_used ()
{
    if (argGasMeter) {
        // the last segment is charged before it runs, so a module that ran out
        // reports slightly more than the limit it was given
        fprintf(stderr, "Gas used: %0.4f\n", m3_GetGasUsed(runtime));
    }
}

void print_backtrace ()
{
    IM3BacktraceInfo info = m3_GetBacktrace(runtime);
    // A trap raised before any frame was entered - a failed function lookup, an
    // exception that unwound past the entry - records nothing, and a header with
    // no frames under it is worse than no header at all
    if (!info || (!info->frames && info->lastFrame != M3_BACKTRACE_TRUNCATED)) {
        return;
    }

    fprintf(stderr, "==== wasm backtrace:");

    int               frameCount = 0;
    IM3BacktraceFrame curr       = info->frames;
    while (curr) {
        fprintf(stderr, "\n  %d: 0x%06x - %s!%s",
                frameCount, curr->moduleOffset,
                m3_GetModuleName(m3_GetFunctionModule(curr->function)),
                m3_GetFunctionName(curr->function));
        curr = curr->next;
        frameCount++;
    }
    if (info->lastFrame == M3_BACKTRACE_TRUNCATED) {
        fprintf(stderr, "\n  (truncated)");
    }
    fprintf(stderr, "\n");
}

M3Result repl_print_results (IM3Function func)
{
    int ret_count = m3_GetRetCount(func);

    static uint64_t    valbuff[128];
    static const void* valptrs[128];
    memset(valbuff, 0, sizeof(valbuff));
    for (int i = 0; i < ret_count; i++) {
        valptrs[i] = &valbuff[i];
    }
    M3Result result = m3_GetResults(func, ret_count, valptrs);
    if (result) {
        return result;
    }

    if (ret_count <= 0) {
        fprintf(stderr, "Result: <Empty Stack>\n");
    }
    for (int i = 0; i < ret_count; i++) {
        switch (m3_GetRetType(func, i)) {
        case c_m3Type_i32: fprintf(stderr, "Result: %" PRIi32 "\n", *(i32*)valptrs[i]); break;
        case c_m3Type_i64: fprintf(stderr, "Result: %" PRIi64 "\n", *(i64*)valptrs[i]); break;
#if d_m3HasFloat
        case c_m3Type_f32: fprintf(stderr, "Result: %" PRIf32 "\n", *(f32*)valptrs[i]); break;
        case c_m3Type_f64: fprintf(stderr, "Result: %" PRIf64 "\n", *(f64*)valptrs[i]); break;
#endif
        default: return "unknown return type";
        }
    }

    return m3Err_none;
}

M3Result repl_call (const char* name, int argc, const char* argv[])
{
    IM3Function func;
    M3Result    result = m3_FindFunction(&func, runtime, name);
    if (result) {
        return result;
    }

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

        if (result == m3Err_trapWasiExit) {
            exit(wasi_ctx->exit_code);
        }
        // m3Err_trapExit (libc exit) is returned below and reported as an error
        if (result) {
            return result;
        }

        // A WASI command's _start takes and returns nothing, but a plain
        // module can export _start with results. Print those instead of
        // silently dropping them.
        if (m3_GetRetCount(func) > 0) {
            result = repl_print_results(func);
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

    result = m3_CallArgv(func, argc, argv);

    print_gas_used();

    if (result) {
        return result;
    }

    return repl_print_results(func);
}

// A reference is an opaque pointer-sized word to the engine, with 0 for null, so
// the host picks how to encode the (ref.extern N) values the spec tests use. We
// hand out N+1 as the handle, which keeps 0 free to mean null.
// The repl's own argument parsing, kept strict for the same reason as
// m3_CallArgv's: a value it cannot read is a mistake, not zero. Floats arrive
// here as bit patterns rather than as decimals, so everything is an integer.
static
M3Result parse_u64 (const char* s, unsigned numBits, uint64_t* o_value)
{
    if (!s || !*s) {
        return "empty argument";
    }
    if (isspace((unsigned char)*s)) {
        return "argument is not a number";
    }

    char*    end = NULL;
    uint64_t value;

    errno = 0;

    if (*s == '-') {
        long long signedValue = strtoll(s, &end, 10);
        if (numBits == 32 && (signedValue < INT32_MIN || signedValue > INT32_MAX)) {
            return "argument out of range";
        }
        value = (uint64_t)signedValue;
    } else {
        value = strtoull(s, &end, 10);
        if (numBits == 32 && value > UINT32_MAX) {
            return "argument out of range";
        }
    }

    if (errno == ERANGE) {
        return "argument out of range";
    }
    if (end == s || *end) {
        return "argument is not a number";
    }

    *o_value = value;
    return m3Err_none;
}

// A reference is the null reference, or a host handle written as an integer.
// The +1 keeps 0 free to mean null; print_ref undoes it.
static
M3Result parse_ref (const char* s, uintptr_t* o_ref)
{
    if (s && strcmp(s, "null") == 0) {
        *o_ref = 0;
        return m3Err_none;
    }

    uint64_t value  = 0;
    M3Result result = parse_u64(s, 64, &value);
    if (result) {
        return result;
    }

    *o_ref = (uintptr_t)value + 1;
    return m3Err_none;
}

static
void print_ref (uintptr_t ref, const char* type)
{
    if (ref) {
        fprintf(stderr, "%" PRIu64 ":%s", (uint64_t)(ref - 1), type);
    } else {
        fprintf(stderr, "null:%s", type);
    }
}

// :invoke is used by spec tests, so it treats floats as raw data
static
IM3Module repl_find_module (const char* id);

// i_module names the module whose export to call, or is NULL to search every
// loaded module (most recently loaded first).
M3Result repl_invoke (const char* i_module, const char* name, int argc, const char* argv[])
{
    IM3Function func;
    M3Result    result;

    if (i_module) {
        IM3Module mod = repl_find_module(i_module);
        if (!mod) {
            return "module not found";
        }

        result = m3_FindFunctionIn(&func, mod, name);
    } else {
        result = m3_FindFunction(&func, runtime, name);
    }

    if (result) {
        return result;
    }

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
    memset((void*)valptrs, 0, sizeof(valptrs));

    for (int i = 0; i < argc; i++) {
        u64*      s     = &valbuff[i];
        uint64_t  value = 0;
        uintptr_t ref   = 0;
        valptrs[i]      = s;
        switch (m3_GetArgType(func, i)) {
        case c_m3Type_i32:
        case c_m3Type_f32:
            result     = parse_u64(argv[i], 32, &value);
            *(u32*)(s) = (u32)value;
            break;
        case c_m3Type_i64:
        case c_m3Type_f64:
            result     = parse_u64(argv[i], 64, &value);
            *(u64*)(s) = value;
            break;
        case c_m3Type_funcref:
        case c_m3Type_externref:
            result           = parse_ref(argv[i], &ref);
            *(uintptr_t*)(s) = ref;
            break;
        default: return "unknown argument type";
        }
        if (result) {
            return result;
        }
    }

    result = m3_Call(func, argc, valptrs);
    if (result) {
        return result;
    }

    // reuse valbuff for return values
    memset(valbuff, 0, sizeof(valbuff));
    for (int i = 0; i < ret_count; i++) {
        valptrs[i] = &valbuff[i];
    }
    result = m3_GetResults(func, ret_count, valptrs);
    if (result) {
        return result;
    }

    fprintf(stderr, "Result: ");
    if (ret_count <= 0) {
        fprintf(stderr, "<Empty Stack>");
    }
    for (int i = 0; i < ret_count; i++) {
        // clang-format off
        switch (m3_GetRetType(func, i)) {
        case c_m3Type_i32: fprintf (stderr, "%" PRIu32 ":i32", *(u32*)valptrs[i]);  break;
        case c_m3Type_f32: fprintf (stderr, "%" PRIu32 ":f32", *(u32*)valptrs[i]);  break;
        case c_m3Type_i64: fprintf (stderr, "%" PRIu64 ":i64", *(u64*)valptrs[i]);  break;
        case c_m3Type_f64: fprintf (stderr, "%" PRIu64 ":f64", *(u64*)valptrs[i]);  break;
        case c_m3Type_funcref:   print_ref (*(uintptr_t*)valptrs[i], "funcref");   break;
        case c_m3Type_externref: print_ref (*(uintptr_t*)valptrs[i], "externref"); break;
        case c_m3Type_exnref:    print_ref (*(uintptr_t*)valptrs[i], "exnref");    break;
        default: return "unknown return type";
        }
        // clang-format on
        if (i != ret_count - 1) {
            fprintf(stderr, ", ");
        }
    }
    fprintf(stderr, "\n");

    return result;
}

// m3_SetModuleName does not take ownership, and the name has to outlive the
// command line it came from, so the names live here and are reused after :init
static char registeredNames[MAX_MODULES][32];
static int  numRegisteredNames = 0;

// A spec test addresses a module by the variable name its .wast gave it ($Mf),
// which is a different thing from the name it is registered under for other
// modules to import from (Mf) - one module can have both, or neither. Only the
// test harness cares about the first, so the mapping lives here rather than in
// M3Module.
static struct {
    char      id[32];
    IM3Module module;
} moduleIds[MAX_MODULES];
static int numModuleIds = 0;

static
IM3Module module_by_id (const char* id)
{
    for (int i = numModuleIds - 1; i >= 0; i--) {
        if (!strcmp(moduleIds[i].id, id)) {
            return moduleIds[i].module;
        }
    }

    return NULL;
}

// Resolves what a test means by a module: its .wast variable if it has one,
// otherwise a registered name.
static
IM3Module repl_find_module (const char* id)
{
    IM3Module module = module_by_id(id);

    return module ? module : m3_FindModule(runtime, id);
}

// Binds the .wast variable name of the module that was just loaded
M3Result repl_name (const char* id)
{
    if (!lastLoadedModule) {
        return "no modules loaded";
    }
    if (numModuleIds >= MAX_MODULES) {
        return "too many named modules";
    }

    snprintf(moduleIds[numModuleIds].id, sizeof(moduleIds[0].id), "%s", id);
    moduleIds[numModuleIds].module = lastLoadedModule;
    numModuleIds++;

    return m3Err_none;
}

// Names a module so later modules can import from it. Without an id that is the
// most recently loaded one, which is what a bare (register "...") means.
M3Result repl_register (const char* name, const char* id)
{
    IM3Module module = id ? repl_find_module(id) : lastLoadedModule;

    if (!module) {
        return "no modules loaded";
    }
    if (numRegisteredNames >= MAX_MODULES) {
        return "too many registered modules";
    }

    char* slot = registeredNames[numRegisteredNames++];
    snprintf(slot, sizeof(registeredNames[0]), "%s", name);

    m3_SetModuleName(module, slot);
    return m3Err_none;
}

M3Result repl_global_get (const char* i_module, const char* name)
{
    IM3Module mod = i_module ? repl_find_module(i_module) : lastLoadedModule;
    if (!mod) {
        return "module not found";
    }

    IM3Global g = m3_FindGlobal(mod, name);

    M3TaggedValue tagged;
    M3Result      err = m3_GetGlobal(g, &tagged);
    if (err) {
        return err;
    }

    // "Result: " so spec-test output parses the same as :invoke
    fprintf(stderr, "Result: ");

    // clang-format off
    switch (tagged.type) {
    case c_m3Type_i32:  fprintf (stderr, "%" PRIu32 ":i32", tagged.value.i32);  break;
    case c_m3Type_i64:  fprintf (stderr, "%" PRIu64 ":i64", tagged.value.i64);  break;
    case c_m3Type_f32:  fprintf (stderr, "%" PRIf32 ":f32", tagged.value.f32);  break;
    case c_m3Type_f64:  fprintf (stderr, "%" PRIf64 ":f64", tagged.value.f64);  break;
    case c_m3Type_funcref:   print_ref ((uintptr_t) tagged.value.i64, "funcref");   break;
    case c_m3Type_externref: print_ref ((uintptr_t) tagged.value.i64, "externref"); break;
    case c_m3Type_exnref:    print_ref ((uintptr_t) tagged.value.i64, "exnref");    break;
    default:            return m3Err_invalidTypeId;
    }
    // clang-format on
    fprintf(stderr, "\n");
    return m3Err_none;
}

M3Result repl_global_set (const char* name, const char* value)
{
    IM3Global g = m3_FindGlobal(lastLoadedModule, name);

    M3TaggedValue tagged = {
        .type = m3_GetGlobalType(g)
    };

    switch (tagged.type) {
    case c_m3Type_i32: tagged.value.i32 = strtoul(value, NULL, 10); break;
    case c_m3Type_i64: tagged.value.i64 = strtoull(value, NULL, 10); break;
    case c_m3Type_f32: tagged.value.f32 = (f32)strtod(value, NULL); break;
    case c_m3Type_f64: tagged.value.f64 = strtod(value, NULL); break;
    default: return m3Err_invalidTypeId;
    }

    return m3_SetGlobal(g, &tagged);
}

M3Result repl_compile ()
{
    return m3_CompileModule(lastLoadedModule);
}

M3Result repl_dump ()
{
    size_t len;
    // the memory of the module most recently loaded, which is the one a repl
    // session is looking at
    uint8_t* mem = m3_GetMemory(lastLoadedModule, &len, 0);
    if (mem) {
        FILE* f = fopen("wasm3_dump.bin", "wb");
        if (!f) {
            return "cannot open file";
        }
        if (fwrite(mem, 1, len, f) != len) {
            fclose(f);
            return "cannot write file";
        }
        fclose(f);
    }
    return m3Err_none;
}

// Gives the module binaries back. Nothing may run wasm afterwards: the loaded
// modules point into them.
static
void release_wasm_bins ()
{
    for (int i = 0; i < wasm_bins_qty; i++) {
        m3_HostUnmapFile(&wasm_bins[i]);
    }
    free(wasm_bins);
    wasm_bins     = NULL;
    wasm_bins_qty = 0;
    wasm_bins_cap = 0;
}

void repl_free ()
{
    if (runtime) {
        m3_HostRemoveInterruptHandler();
        m3_FreeRuntime(runtime);
        runtime = NULL;
    }

    release_wasm_bins();
}

// The spec testsuite expects a module registered as "spectest", exporting the
// print functions plus a (table 10 20 funcref), a (memory 1 2) and four
// globals. Host functions can stand in for the functions but not for the memory
// and the table, so the repl loads the real thing - see spectest.wasm.h.
static bool provideSpecTest = false;

static
M3Result load_spectest (void)
{
    IM3Module module = NULL;

    M3Result result = m3_ParseModule(env, &module, spectest_wasm, spectest_wasm_len);
    if (result) {
        return result;
    }

    result = m3_LoadModule(runtime, module);
    if (result) {
        return result;  // the runtime owns it either way
    }

    lastLoadedModule = module;

    // it has to answer to "spectest" before anything imports from it
    return repl_register("spectest", NULL);
}

M3Result repl_init (unsigned stack)
{
    repl_free();
    numRegisteredNames = 0;
    numModuleIds       = 0;
    lastLoadedModule   = NULL;
    runtime            = m3_NewRuntime(env, stack, NULL);
    if (runtime == NULL) {
        return "m3_NewRuntime failed";
    }

    // likewise armed before anything is compiled: a body compiled while this
    // is off gets back edges that cannot suspend. It is re-armed here rather
    // than once in main because :init builds a new runtime.
    if (argSnapshotFile || argResumeFile) {
        m3_SetSuspendable(runtime, true);
        m3_HostInstallInterruptHandler(runtime);
    }

    // has to be armed before anything is compiled: only the function bodies
    // compiled after this carry the metering instrumentation
    if (argGasMeter) {
        m3_SetGasLimit(runtime, argGasLimit);
    }

    if (provideSpecTest) {
        M3Result result = load_spectest();
        if (result) {
            return result;
        }
    }

    return m3Err_none;
}

static
void unescape (char* buff)
{
    char* outp = buff;
    while (*buff) {
        if (*buff == '\\') {
            switch (*(buff + 1)) {
            case '0': *outp++ = '\0'; break;
            case 'b': *outp++ = '\b'; break;
            case 'n': *outp++ = '\n'; break;
            case 'r': *outp++ = '\r'; break;
            case 't': *outp++ = '\t'; break;
            case 'x': {
                char hex[3] = { *(buff + 2), *(buff + 3), '\0' };

                *outp = (char)strtol(hex, NULL, 16);
                buff += 2;
                outp += 1;
                break;
            }
            // Otherwise just pass the letter
            // Also handles '\\'
            default: *outp++ = *(buff + 1); break;
            }
            buff += 2;
        } else {
            *outp++ = *buff++;
        }
    }
    *outp = '\0';
}

static
int split_argv (char* str, char** argv)
{
    int   result = 0;
    char* curr   = str;
    int   len    = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        if (strchr(" \n\r\t", str[i])) {
            if (len) {  // Found space after non-space
                str[i]         = '\0';
                argv[result++] = curr;
                len            = 0;
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

void print_version ()
{
    const char* wasm3_env  = getenv("WASM3");
    const char* wasm3_arch = getenv("WASM3_ARCH");
    const char* wasi_impl  = NULL;
#if defined(d_m3HasWASI)
    wasi_impl = ", wasi";
#elif defined(d_m3HasUVWASI)
    wasi_impl = ", uvwasi";
#elif defined(d_m3HasMetaWASI)
    wasi_impl = ", metawasi";
#endif

    printf("Wasm3 v" M3_VERSION "%s on %s\n",
           (wasm3_arch || wasm3_env) ? " self-hosting" : "",
           (wasm3_arch) ? wasm3_arch : M3_ARCH);

    // clang-format off
    printf("Build: " __DATE__ " " __TIME__ ", " M3_COMPILER_VER "%s%s%s%s%s%s%s%s%s\n",
            d_m3CanTailCall    ? ", tail-call"     : "",
            d_m3HasTypedRefs   ? ", typed-refs"    : "",
            d_m3HasMultiMemory ? ", multi-memory"  : "",
            d_m3HasWideArithmetic ? ", wide-arithmetic" : "",
            d_m3DeterministicProfile ? ", deterministic" : "",
            d_m3CanonicalNaN   ? ", canonical-nan" : "",
            d_m3GuardedMemory  ? ", guarded-mem"   : "",
            d_m3HasSnapshots   ? ", snapshots"     : "",
            wasi_impl          ? wasi_impl     : ""
    );
    // clang-format on
}

void print_usage ()
{
    puts("Usage:");
    puts("  wasm3 [options] <file> [args...]");
    puts("  wasm3 --repl [file]");
    puts("Options:");
    puts("  --func <function>              function to run       default: _start");
    puts("  --stack-size <size>            stack size in bytes   default: 512KB");
    puts("  --compile                      disable lazy compilation");
    puts("  --validate-only                only validate <file>");
    puts("  --no-validate                  skip validation");
    puts("  --spec-repl                    repl for the spec tests");
    puts("  --dump-on-trap                 save wasm3_dump.dmp on a trap");
    puts("  --gas-meter                    meter gas usage");
    puts("  --gas-limit <gas>              apply gas limit");
    puts("  --snapshot <fn>[:<name>]       enable suspension and save to <fn>");
    puts("  --resume <fn>[:<name>]|none    resume execution from <fn> or disable auto-resume");
    puts("  --interrupt                    pause at the first pause point");
}

static
M3Result FileSnapshotWriter (const void* i_data, size_t i_size, void* i_userdata)
{
    FILE* f = (FILE*)i_userdata;
    if (fwrite(i_data, 1, i_size, f) != i_size) {
        return "failed writing snapshot file";
    }
    return m3Err_none;
}

static
bool ends_with (const char* str, const char* suffix)
{
    if (!str || !suffix) {
        return false;
    }
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (suf_len > str_len) {
        return false;
    }
    return strcmp(str + str_len - suf_len, suffix) == 0;
}

// Writes the suspended execution to i_path and says what the process exits
// with. The snapshot is taken whole before the file is opened: the path can be
// the very snapshot this run resumed from, and a save that fails must not have
// emptied it first.
static
int SaveSuspension (const char* i_path)
{
    void*    bytes  = NULL;
    size_t   size   = 0;
    M3Result result = m3Err_none;

    bool embed = ends_with(i_path, ".wasm");
    if (embed) {
        result = m3_SaveSnapshotToModule(runtime, lastLoadedModule, argSnapshotName, &bytes, &size);
    } else {
        result = m3_SaveSnapshotToBuffer(runtime, &bytes, &size);
    }

    if (result) {
        fprintf(stderr, "Error saving snapshot: %s\n", result);
        return 1;
    }

    // The snapshot is in hand, so the module's bytes are no longer needed - and
    // they are mapped, which on Windows denies the write sharing this open
    // wants. Giving them back first is what lets the path be the module itself.
    release_wasm_bins();

    // Written beside the target and moved over it, so a write that fails - a
    // full disk, a killed process - leaves the old file whole. That old file
    // can be the only copy of the program: a module that checkpoints itself.
    char tmpPath[1024 + 8];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", i_path);

    FILE* f  = fopen(tmpPath, "wb");
    bool  ok = f and fwrite(bytes, 1, size, f) == size;

    if (f and fclose(f) != 0) {
        ok = false;
    }

    free(bytes);

    ok = ok and m3_HostReplaceFile(tmpPath, i_path);

    if (not ok) {
        if (f) {
            remove(tmpPath);
        }
        fprintf(stderr, "Error writing snapshot to %s\n", i_path);
        return 1;
    }

    fprintf(stderr, "Execution suspended. Snapshot %ssaved to %s\n", embed ? "(embedded) " : "", i_path);
    return 0;
}

static
void SaveTrapSnapshot ()
{
    FILE* f = fopen("wasm3_dump.dmp", "wb");
    if (!f) {
        fprintf(stderr, "Error opening wasm3_dump.dmp for writing\n");
        return;
    }

    M3Result result = m3_SaveSnapshot(runtime, FileSnapshotWriter, f);
    fclose(f);
    if (result) {
        fprintf(stderr, "Error saving trap snapshot: %s\n", result);
    } else {
        fprintf(stderr, "Trap snapshot saved to wasm3_dump.dmp\n");
    }
}

static
M3Result FileSnapshotReader (void* o_buffer, size_t i_size, void* i_userdata)
{
    FILE* f = (FILE*)i_userdata;
    if (fread(o_buffer, 1, i_size, f) != i_size) {
        return "failed reading snapshot file";
    }
    return m3Err_none;
}

#define ARGV_SHIFT()  { i_argc--; i_argv++; }
#define ARGV_SET(x)   { if (i_argc > 0) { x = i_argv[0]; ARGV_SHIFT(); } }

int main (int i_argc, const char* i_argv[])
{
    M3Result result = m3Err_none;

    env     = m3_NewEnvironment();
    runtime = NULL;

    bool        argRepl         = false;
    bool        argDumpOnTrap   = false;
    bool        argCompile      = false;
    bool        argValidateOnly = false;
    bool        argNoValidate   = false;
    const char* argFile         = NULL;
    unsigned    argStackSize    = 512 * 1024;

    // m3_PrintM3Info ();

    ARGV_SHIFT(); // Skip executable name

    while (i_argc > 0) {
        const char* arg = i_argv[0];
        if (arg[0] != '-') {
            break;
        }

        ARGV_SHIFT();
        if (!strcmp("--help", arg) or !strcmp("-h", arg)) {
            print_usage();
            return 0;
        } else if (!strcmp("--version", arg)) {
            print_version();
            return 0;
        } else if (!strcmp("--repl", arg)) {
            argRepl = true;
        } else if (!strcmp("--spec-repl", arg)) {
            // repl for the spec tests
            argStackSize    = 64 * 1024;
            argRepl         = true;
            argCompile      = true;
            provideSpecTest = true;
        } else if (!strcmp("--dump-on-trap", arg)) {
            argDumpOnTrap = true;
        } else if (!strcmp("--compile", arg)) {
            argCompile = true;
        } else if (!strcmp("--validate-only", arg)) {
            // validating a file is loading and compiling all of it, and then
            // running none of it
            argValidateOnly = true;
            argCompile      = true;
        } else if (!strcmp("--no-validate", arg)) {
            argNoValidate = true;
        } else if (!strcmp("--stack-size", arg)) {
            const char* tmp = "524288";
            ARGV_SET(tmp);
            argStackSize = atol(tmp);
        } else if (!strcmp("--gas-meter", arg)) {
            argGasMeter = true;
        } else if (!strcmp("--gas-limit", arg)) {
            const char* tmp = "0";
            ARGV_SET(tmp);
            argGasLimit = atof(tmp);
            argGasMeter = true;
        } else if (!strcmp("--dir", arg)) {
            const char* argDir;
            ARGV_SET(argDir);
            (void)argDir;
        } else if (!strcmp("--func", arg) or !strcmp("-f", arg)) {
            ARGV_SET(argFunc);
        } else if (!strcmp("--snapshot", arg)) {
            const char* tmp = NULL;
            ARGV_SET(tmp);
            if (tmp) {
                const char* name = NULL;
                parse_wasm_path(tmp, argSnapshotFileBuf, sizeof(argSnapshotFileBuf), &name);
                argSnapshotFile = argSnapshotFileBuf;
                set_snapshot_name(name, true);
            }
        } else if (!strcmp("--interrupt", arg)) {
            argInterrupt = true;
        } else if (!strcmp("--resume", arg)) {
            const char* tmp = NULL;
            ARGV_SET(tmp);
            if (tmp) {
                if (!strcmp(tmp, "none")) {
                    argResumeNone = true;
                } else {
                    const char* name = NULL;
                    parse_wasm_path(tmp, argResumeFileBuf, sizeof(argResumeFileBuf), &name);
                    argResumeFile = argResumeFileBuf;
                    set_snapshot_name(name, true);
                }
            }
        }
    }

    if (not argRepl and i_argc < 1 and argResumeFile) {
        argFile = argResumeFile;
    } else if ((argRepl and (i_argc > 1)) or   // repl supports 0 or 1 args
               (not argRepl and (i_argc < 1))  // normal expects at least 1
    ) {
        if (launched_from_gui_shell()) {
            print_version();
            print_usage();
            // Pause so the user can read the output before the console closes
            fprintf(stderr, "Press Enter to exit...");
            getchar();
        } else {
            print_usage();
        }
        return 1;
    }

    if (!argFile && i_argc > 0) {
        const char* tmp = NULL;
        ARGV_SET(tmp);
        if (tmp) {
            const char* name = NULL;
            parse_wasm_path(tmp, argFileBuf, sizeof(argFileBuf), &name);
            argFile = argFileBuf;
            set_snapshot_name(name, false);
        }
    }

    if (argValidateOnly) {
#if d_m3EnableValidation
        if (!argFile) {
            print_usage();
            return 1;
        }
        if (argNoValidate) {
            fprintf(stderr, "Error: --no-validate contradicts --validate-only\n");
            return 1;
        }
#else
        fprintf(stderr, "Error: validation not available in this build of Wasm3\n");
        return 1;
#endif
    }

#if !d_m3HasGasMetering
    if (argGasMeter) {
        fprintf(stderr, "Error: gas metering not available in this build of Wasm3\n");
        return 1;
    }
#endif

    result = repl_init(argStackSize);
    if (result) {
        FATAL("repl_init: %s", result);
    }

    m3_SetValidation(runtime, not argNoValidate);

    if (argSnapshotFile || argResumeFile) {
        m3_SetSuspendable(runtime, true);
    }

    //if (argGasMeter) {
    //    fprintf(stderr, "Warning: Gas is limited to %0.4f\n", m3_GetGasLimit(runtime));
    //}

    if (argFile) {
        argAutoResume = not argRepl and not argResumeNone and not argResumeFile and (not argFunc or !strcmp(argFunc, "_start"));

        result = repl_load(argFile);
        if (result) {
            FATAL("repl_load: %s", result);
        }

        if (argCompile || argResumeFile || argResumeEmbedded) {
            result = repl_compile();
            if (result) {
                FATAL("repl_compile: %s", result);
            }
        }

        if (argValidateOnly) {
            // it parsed, it instantiated and every function body compiled
            repl_free();
            m3_FreeEnvironment(env);
            return 0;
        }

        if (argResumeFile || argResumeEmbedded) {
            if (argResumeEmbedded || argResumeFile == argFile || ends_with(argResumeFile, ".wasm")) {
                if (m3_HasSnapshot(lastLoadedModule, argSnapshotName)) {
                    result = m3_LoadEmbeddedSnapshot(runtime, lastLoadedModule, argSnapshotName);
                } else if (argResumeFile && argResumeFile != argFile) {
                    // The snapshot rides in another .wasm. Only its bytes come
                    // from there - it still restores into the module this run
                    // loaded, so that is the module handed to the loader.
                    M3HostFile bin;
                    result = read_wasm_file(argResumeFile, &bin);
                    if (!result) {
                        IM3Module donor = NULL;
                        result          = m3_ParseModule(env, &donor, (const u8*)bin.data, (u32)bin.size);
                        if (!result) {
                            const void* snapshot = NULL;
                            size_t      size     = 0;

                            result = m3_GetEmbeddedSnapshot(donor, argSnapshotName, &snapshot, &size);
                            if (!result) {
                                result = m3_LoadSnapshotFromBuffer(runtime, lastLoadedModule, snapshot, size);
                            }
                            // the snapshot points into bin, so the donor goes
                            // only once it has been read
                            m3_FreeModule(donor);
                        }
                        m3_HostUnmapFile(&bin);
                    }
                } else {
                    result = "module contains no embedded snapshot";
                }
            } else {
                FILE* f = fopen(argResumeFile, "rb");
                if (!f) {
                    FATAL("cannot open snapshot file: %s", argResumeFile);
                }
                result = m3_LoadSnapshot(runtime, lastLoadedModule, FileSnapshotReader, f);
                fclose(f);
            }
            if (result) {
                FATAL("failed loading snapshot: %s", result);
            }

            if (argInterrupt) {
                m3_RequestSuspend(runtime);
            }

            result = m3_ResumeRuntime(runtime);
            if (result == m3Err_continuationSuspended) {
                int status = SaveSuspension(argSnapshotFile     ? argSnapshotFile
                                            : argResumeEmbedded ? argFile
                                                                : argResumeFile);
                m3_HostRemoveInterruptHandler();
                repl_free();
                m3_FreeEnvironment(env);
                return status;
            }

            if (result) {
                if (argDumpOnTrap) {
                    SaveTrapSnapshot();
                }
                print_backtrace();
                goto _onfatal;
            }

            m3_HostRemoveInterruptHandler();
            repl_free();
            m3_FreeEnvironment(env);
            return 0;
        }

        if (argFunc and not argRepl) {
            if (argInterrupt) {
                m3_RequestSuspend(runtime);
            }

            if (!strcmp(argFunc, "_start")) {
                // When passing args to WASI, include wasm filename as argv[0]
                result = repl_call(argFunc, i_argc + 1, i_argv - 1);
            } else {
                result = repl_call(argFunc, i_argc, i_argv);
            }

            // only --snapshot makes this runtime suspendable here: --resume
            // never reaches this far
            if (result == m3Err_continuationSuspended) {
                int status = SaveSuspension(argSnapshotFile);
                m3_HostRemoveInterruptHandler();
                repl_free();
                m3_FreeEnvironment(env);
                return status;
            }

            if (result) {
                if (argDumpOnTrap) {
                    SaveTrapSnapshot();
                }
                print_backtrace();
                goto _onfatal;
            }
        }
    }

    while (argRepl) {
        char  cmd_buff[2048] = { 0 };
        char* argv[32]       = { 0 };
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

#define NEED_ARGS(N)   if (argc < (N)) { result = "not enough arguments"; } else

        if (!strcmp(":init", argv[0])) {
            result = repl_init(argStackSize);
            m3_SetValidation(runtime, not argNoValidate);
        } else if (!strcmp(":version", argv[0])) {
            print_version();
        } else if (!strcmp(":exit", argv[0])) {
            repl_free();
            return 0;
        } else if (!strcmp(":load", argv[0])) {             // :load <filename>
            NEED_ARGS(2)
            {
                result = repl_load(argv[1]);
                if (argCompile and not result) {
                    result = repl_compile();
                }
            }
        } else if (!strcmp(":load-hex", argv[0])) {         // :load-hex <size>\n <hex-encoded-binary>
            NEED_ARGS(2)
            {
                result = repl_load_hex(atol(argv[1]));
                if (argCompile and not result) {
                    result = repl_compile();
                }
            }
        } else if (!strcmp(":get-global", argv[0])) {       // :get-global <global>
            NEED_ARGS(2)
            {
                unescape(argv[1]);
                result = repl_global_get(NULL, argv[1]);
            }
        } else if (!strcmp(":get-global-in", argv[0])) {    // :get-global-in <module> <global>
            NEED_ARGS(3)
            {
                unescape(argv[1]);
                unescape(argv[2]);
                result = repl_global_get(argv[1], argv[2]);
            }
        } else if (!strcmp(":set-global", argv[0])) {       // :set-global <global> <value>
            NEED_ARGS(3)
            {
                result = repl_global_set(argv[1], argv[2]);
            }
        } else if (!strcmp(":register", argv[0])) {         // :register <name> [module]
            NEED_ARGS(2)
            {
                result = repl_register(argv[1], argc > 2 ? argv[2] : NULL);
            }
        } else if (!strcmp(":name", argv[0])) {             // :name <module>
            NEED_ARGS(2)
            {
                result = repl_name(argv[1]);
            }
        } else if (!strcmp(":dump", argv[0])) {
            result = repl_dump();
        } else if (!strcmp(":compile", argv[0])) {
            result = repl_compile();
        } else if (!strcmp(":invoke", argv[0])) {           // :invoke <function> [args...]
            NEED_ARGS(2)
            {
                unescape(argv[1]);
                result = repl_invoke(NULL, argv[1], argc - 2, (const char**)(argv + 2));
            }
        } else if (!strcmp(":invoke-in", argv[0])) {        // :invoke-in <module> <function> [args...]
            NEED_ARGS(3)
            {
                unescape(argv[1]);
                unescape(argv[2]);
                result = repl_invoke(argv[1], argv[2], argc - 3, (const char**)(argv + 3));
            }
        } else if (argv[0][0] == ':') {
            result = "no such command";
        } else {
            unescape(argv[0]);
            result = repl_call(argv[0], argc - 1, (const char**)(argv + 1));
            if (result) {
                print_backtrace();
            }
        }

        if (result == m3Err_trapWasiExit) {
#if defined(LINK_WASI)
            // the exit code lives on the WASI context, the same one the non-repl
            // path exits with; in the repl the session continues, so report it
            fprintf(stderr, M3_ARCH "-wasi: exit(%d)\n", m3_GetWasiContext()->exit_code);
#endif
        } else if (result) {
            fprintf(stderr, "Error: %s", result);
            M3ErrorInfo info;
            m3_GetErrorInfo(runtime, &info);
            fprintf(stderr, " (%s)\n", info.message);
        }
    }

_onfatal:
    if (result) {
        fprintf(stderr, "Error: %s", result);
        if (runtime) {
            M3ErrorInfo info;
            m3_GetErrorInfo(runtime, &info);
            if (strlen(info.message)) {
                fprintf(stderr, " (%s)", info.message);
            }
        }
        fprintf(stderr, "\n");
    }

    m3_HostRemoveInterruptHandler();
    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);

    return result ? 1 : 0;
}
