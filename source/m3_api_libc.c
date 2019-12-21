//
//  m3_api_libc.c
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#include "m3_api_libc.h"

#include "m3_api_defs.h"
#include "m3_env.h"
#include "m3_module.h"
#include "m3_exception.h"

#include <time.h>
#include <errno.h>
#include <stdio.h>


#if defined(WIN32)

#include <Windows.h>

int clock_gettime(int clk_id, struct timespec *spec)
{
    __int64 wintime;
    GetSystemTimeAsFileTime((FILETIME*)&wintime);
    wintime      -=116444736000000000i64;           //1jan1601 to 1jan1970
    spec->tv_sec  =wintime / 10000000i64;           //seconds
    spec->tv_nsec =wintime % 10000000i64 *100;      //nano-seconds
    return 0;
}

#endif


// TODO: return trap
void  m3_libc_abort()
{
    abort ();
}

m3ret_t m3_libc_exit (i32 i_code)
{
    printf ("exit (%d)\n", i_code);

    return c_m3Err_trapExit;
}

void* m3_libc_memset(void * i_ptr, i32 i_value, i32 i_size)
{
    memset (i_ptr, i_value, i_size);
    return i_ptr;
}


void* m3_libc_memcpy(void * o_dst, void * i_src, i32 i_size)
{
    return memcpy (o_dst, i_src, i_size);
}

uint32_t m3_libc_clock()
{
    return clock();
}

uint32_t m3_libc_clock_gettime(uint32_t clk_id, struct timespec* tp)
{
    return clock_gettime(clk_id, tp);
}


static
M3Result  SuppressLookupFailure (M3Result i_result)
{
    if (i_result == c_m3Err_functionLookupFailed)
        return c_m3Err_none;
    else
        return i_result;
}

M3Result  m3_LinkLibC  (IM3Module module)
{
    M3Result result = c_m3Err_none;

    const char* namespace = "env";

//_   (SuppressLookupFailure (m3_LinkFunction (module, "_printf",           "v(**)",   &m3_libc_printf)));
//_   (SuppressLookupFailure (m3_LinkFunction (module, "g$_stderr",         "i(M)",    &m3_libc_get_stderr)));

_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "_memset",           "*(*ii)",  &m3_libc_memset)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "_memcpy",           "*(**i)",  &m3_libc_memcpy)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "_exit",             "Tv(i)",   &m3_libc_exit)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "_perror",           "v(*)",    &perror)));

_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "_clock",            "i()",     &m3_libc_clock)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "_clock_gettime",    "i(i*)",   &m3_libc_clock_gettime)));


_catch:
    return result;
}

