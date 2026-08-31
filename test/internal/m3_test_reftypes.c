//
//  m3_test_reftypes.c
//
//  Exercises the reference-type embedding API: the 'r'/'R' link signature
//  characters, and funcref/externref values crossing m3_Call/m3_GetResults.
//
//  Build:  cc -I ../../source -o m3_test_reftypes m3_test_reftypes.c libm3.a -lm
//

#include <stdio.h>

#include "wasm3.h"

static int failures = 0;

#define expect(TEST, ...) do {                                          \
        if (TEST) { printf ("ok:   " __VA_ARGS__); }                    \
        else      { printf ("FAIL: " __VA_ARGS__); failures++; }        \
        printf ("\n");                                                  \
    } while (0)

//  (module
//    (import "env" "take_ref" (func $take (param externref) (result externref)))
//    (func (export "roundtrip") (param externref) (result externref)
//      local.get 0
//      call $take)
//    (func (export "make_null") (result externref)
//      ref.null extern))
// clang-format off
static const unsigned char c_module [] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x02, 0x60,
    0x01, 0x6f, 0x01, 0x6f, 0x60, 0x00, 0x01, 0x6f, 0x02, 0x10, 0x01, 0x03,
    0x65, 0x6e, 0x76, 0x08, 0x74, 0x61, 0x6b, 0x65, 0x5f, 0x72, 0x65, 0x66,
    0x00, 0x00, 0x03, 0x03, 0x02, 0x00, 0x01, 0x07, 0x19, 0x02, 0x09, 0x72,
    0x6f, 0x75, 0x6e, 0x64, 0x74, 0x72, 0x69, 0x70, 0x00, 0x01, 0x09, 0x6d,
    0x61, 0x6b, 0x65, 0x5f, 0x6e, 0x75, 0x6c, 0x6c, 0x00, 0x02, 0x0a, 0x0d,
    0x02, 0x06, 0x00, 0x20, 0x00, 0x10, 0x00, 0x0b, 0x04, 0x00, 0xd0, 0x6f,
    0x0b,
};
// clang-format on

static const void* g_seenByHost = (const void*)0xdeadbeef;

m3ApiRawFunction(host_take_ref)
{
    m3ApiReturnType(const void*)
    m3ApiGetArg(const void*, ref)

    g_seenByHost = ref;

    m3ApiReturn(ref);
}

int main (int i_argc, const char* i_argv[])
{
    IM3Environment env     = m3_NewEnvironment();
    IM3Runtime     runtime = m3_NewRuntime(env, 64 * 1024, NULL);

    IM3Module module;
    M3Result  result = m3_ParseModule(env, &module, c_module, sizeof(c_module));
    expect(!result, "parse module (%s)", result ? result : "ok");
    if (result) {
        return 1;
    }

    result = m3_LoadModule(runtime, module);
    expect(!result, "load module (%s)", result ? result : "ok");

    result = m3_LinkRawFunction(module, "env", "take_ref", "R(R)", &host_take_ref);
    expect(!result, "link with signature \"R(R)\" (%s)", result ? result : "ok");
    if (result) {
        return 1;
    }

    {   // a signature that doesn't match the import must still be rejected
        IM3Module  other;
        IM3Runtime otherRuntime = m3_NewRuntime(env, 64 * 1024, NULL);
        m3_ParseModule(env, &other, c_module, sizeof(c_module));
        m3_LoadModule(otherRuntime, other);

        result = m3_LinkRawFunction(other, "env", "take_ref", "i(i)", &host_take_ref);
        expect(result != NULL, "signature \"i(i)\" is rejected (%s)", result ? result : "accepted!");

        m3_FreeRuntime(otherRuntime);
    }

    IM3Function roundtrip, makeNull;
    result = m3_FindFunction(&roundtrip, runtime, "roundtrip");
    expect(!result, "find roundtrip (%s)", result ? result : "ok");
    result = m3_FindFunction(&makeNull, runtime, "make_null");
    expect(!result, "find make_null (%s)", result ? result : "ok");

    expect(m3_GetArgType(roundtrip, 0) == c_m3Type_externref, "arg type is externref");
    expect(m3_GetRetType(roundtrip, 0) == c_m3Type_externref, "ret type is externref");

    {   // a non-null externref makes the full round trip through the host
        const void* handle  = (const void*)0x1234;
        const void* args[1] = { &handle };

        result = m3_Call(roundtrip, 1, args);
        expect(!result, "call roundtrip (%s)", result ? result : "ok");

        const void* out     = (const void*)0xbadbad;
        const void* rets[1] = { &out };

        result = m3_GetResults(roundtrip, 1, rets);
        expect(!result, "get results (%s)", result ? result : "ok");

        expect(g_seenByHost == handle, "host received the reference (%p)", g_seenByHost);
        expect(out == handle, "wasm returned the same reference (%p)", out);
    }

    {   // ref.null must surface as NULL
        const void* out     = (const void*)0xbadbad;
        const void* rets[1] = { &out };

        result = m3_Call(makeNull, 0, NULL);
        expect(!result, "call make_null (%s)", result ? result : "ok");

        result = m3_GetResults(makeNull, 1, rets);
        expect(!result && out == NULL, "ref.null arrives as NULL (%p)", out);
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
