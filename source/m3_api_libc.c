//
//  m3_api_libc.c
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#define _POSIX_C_SOURCE 200809L

#include "m3_api_libc.h"

#include "m3_api_defs.h"
#include "m3_env.h"
#include "m3_exception.h"

#include <time.h>
#include <errno.h>
#include <stdio.h>


m3ApiRawFunction(m3_libc_abort)
{
    m3ApiTrap(m3Err_trapAbort);
}

m3ApiRawFunction(m3_libc_exit)
{
    m3ApiGetArg     (int32_t, code)

    m3ApiTrap(m3Err_trapExit);
}


m3ApiRawFunction(m3_libc_memset)
{
    m3ApiReturnType (int32_t)

    m3ApiGetArgMem  (void*,   i_ptr)
    m3ApiGetArg     (int32_t, i_value)
    m3ApiGetArg     (int32_t, i_size)

    m3ApiCheckMem(i_ptr, i_size);

    u32 result = m3ApiPtrToOffset(memset (i_ptr, i_value, i_size));
    m3ApiReturn(result);
}

m3ApiRawFunction(m3_libc_memmove)
{
    m3ApiReturnType (int32_t)

    m3ApiGetArgMem  (void*,   o_dst)
    m3ApiGetArgMem  (void*,   i_src)
    m3ApiGetArg     (int32_t, i_size)

    m3ApiCheckMem(o_dst, i_size);
    m3ApiCheckMem(i_src, i_size);

    u32 result = m3ApiPtrToOffset(memmove (o_dst, i_src, i_size));
    m3ApiReturn(result);
}

m3ApiRawFunction(m3_libc_print)
{
    m3ApiReturnType (uint32_t)

    m3ApiGetArgMem  (void*,    i_ptr)
    m3ApiGetArg     (uint32_t, i_size)

    m3ApiCheckMem(i_ptr, i_size);

    fwrite(i_ptr, i_size, 1, stdout);
    fflush(stdout);

    m3ApiReturn(i_size);
}

m3ApiRawFunction(m3_libc_clock_ms)
{
    m3ApiReturnType (uint32_t)

    m3ApiReturn(clock() / (CLOCKS_PER_SEC/1000));
}

static
M3Result  SuppressLookupFailure (M3Result i_result)
{
    if (i_result == m3Err_functionLookupFailed)
        return m3Err_none;
    else
        return i_result;
}

m3ApiRawFunction(m3_spectest_dummy)
{
    m3ApiSuccess();
}

M3Result  m3_LinkSpecTest  (IM3Module module)
{
    M3Result result = m3Err_none;

    const char* spectest = "spectest";

_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print",         "v()",      &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_i32",     "v(i)",     &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_i64",     "v(I)",     &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_f32",     "v(f)",     &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_f64",     "v(F)",     &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_i32_f32", "v(if)",    &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_i64_f64", "v(IF)",    &m3_spectest_dummy)));

_catch:
    return result;
}


M3Result  m3_LinkLibC  (IM3Module module)
{
    M3Result result = m3Err_none;

    const char* env = "env";

_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_debug",            "i(*i)",   &m3_libc_print)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_memset",           "*(*ii)",  &m3_libc_memset)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_memmove",          "*(**i)",  &m3_libc_memmove)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_memcpy",           "*(**i)",  &m3_libc_memmove))); // just alias of memmove
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_abort",            "v()",     &m3_libc_abort)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_exit",             "v(i)",    &m3_libc_exit)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "clock_ms",          "i()",     &m3_libc_clock_ms)));

_catch:
    return result;
}

