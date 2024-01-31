#include "m3_api_ubsan.h"

#include "m3_env.h"
#include "m3_exception.h"

#if defined(d_m3HasUBSan)

// extern "C" void* emscripten_return_address(int level);
// https://github.com/quantum5/emscripten/blob/c5fa1768e4826e603bc6158f6b0f26dbdb6d150b/system/lib/compiler-rt/lib/ubsan_minimal/ubsan_minimal_handlers.cpp#L86
m3ApiRawFunction(m3_emscripten_return_address)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, level)
    m3ApiReturn(1234);
    //m3ApiTrap(m3Err_trapDivisionByZero);
}

static
M3Result  SuppressLookupFailure(M3Result i_result)
{
    if (i_result == m3Err_functionLookupFailed)
        return m3Err_none;
    else
        return i_result;
}

M3Result  m3_LinkUBSan(IM3Module module)
{
    M3Result result = m3Err_none;

    const char* env = "env";

_   (SuppressLookupFailure(m3_LinkRawFunction(module, env, "emscripten_return_address", "*(i)", &m3_emscripten_return_address)));

_catch:
    return result;
}

#endif // d_m3HasUBSan