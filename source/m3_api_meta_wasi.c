//
//  m3_api_meta_wasi.c
//
//  Created by Volodymyr Shymanskyy on 01/08/20.
//  Copyright © 2020 Volodymyr Shymanskyy. All rights reserved.
//

#include "m3_api_wasi.h"

#include "m3_api_defs.h"
#include "m3_env.h"
#include "m3_exception.h"

#if defined(d_m3HasMetaWASI)

// NOTE: MetaWASI mostly redirects WASI calls to the host WASI environment

#if !defined(__wasi__)
# error "MetaWASI is only supported on WASI target"
#endif

#include <wasi/core.h>
typedef size_t __wasi_size_t;

static inline
void copy_iov_to_host(void* _mem, __wasi_iovec_t* host_iov, __wasi_iovec_t* wasi_iov, int32_t iovs_len)
{
    // Convert wasi memory offsets to host addresses
    for (int i = 0; i < iovs_len; i++) {
        host_iov[i].buf = m3ApiOffsetToPtr(wasi_iov[i].buf);
        host_iov[i].buf_len  = wasi_iov[i].buf_len;
    }
}

/*
 * WASI API implementation
 */

m3ApiRawFunction(m3_wasi_unstable_args_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (u32*                 , argv)
    m3ApiGetArgMem   (char*                , argv_buf)

    if (runtime == NULL) { m3ApiReturn(__WASI_EINVAL); }

    for (u32 i = 0; i < runtime->argc; ++i)
    {
        argv[i] = m3ApiPtrToOffset (argv_buf);

        size_t len = strlen (runtime->argv [i]);
        memcpy (argv_buf, runtime->argv [i], len);
        argv_buf += len;
        * argv_buf++ = 0;
    }

    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_args_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (__wasi_size_t*       , argc)
    m3ApiGetArgMem   (__wasi_size_t*       , argv_buf_size)

    if (runtime == NULL) { m3ApiReturn(__WASI_EINVAL); }

    *argc = runtime->argc;
    *argv_buf_size = 0;
    for (u32 i = 0; i < runtime->argc; ++i)
    {
        * argv_buf_size += strlen (runtime->argv [i]) + 1;
    }
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_environ_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (u32*                 , env)
    m3ApiGetArgMem   (char*                , env_buf)

    __wasi_errno_t ret;
    __wasi_size_t env_count, env_buf_size;

    ret = __wasi_environ_sizes_get(&env_count, &env_buf_size);
    if (ret != __WASI_ESUCCESS) m3ApiReturn(ret);

    ret = __wasi_environ_get(env, env_buf);
    if (ret != __WASI_ESUCCESS) m3ApiReturn(ret);

    for (u32 i = 0; i < env_count; ++i) {
        env[i] = m3ApiPtrToOffset (env[i]);
    }

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_environ_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (__wasi_size_t*       , env_count)
    m3ApiGetArgMem   (__wasi_size_t*       , env_buf_size)

    __wasi_errno_t ret = __wasi_environ_sizes_get(env_count, env_buf_size);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_dir_name)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (char*                , path)
    m3ApiGetArg      (__wasi_size_t        , path_len)

    __wasi_errno_t ret = __wasi_fd_prestat_dir_name(fd, path, path_len);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_prestat_t*    , buf)

    __wasi_errno_t ret = __wasi_fd_prestat_get(fd, buf);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_fdstat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_fdstat_t*     , fdstat)

    __wasi_errno_t ret = __wasi_fd_fdstat_get(fd, fdstat);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_fdstat_set_flags)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_fdflags_t     , flags)

    __wasi_errno_t ret = __wasi_fd_fdstat_set_flags(fd, flags);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_seek)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_filedelta_t   , offset)
    m3ApiGetArg      (__wasi_whence_t      , whence)
    m3ApiGetArgMem   (__wasi_filesize_t*   , result)

    __wasi_errno_t ret = __wasi_fd_seek(fd, offset, whence, result);

    m3ApiReturn(ret);
}


m3ApiRawFunction(m3_wasi_unstable_path_open)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , dirfd)
    m3ApiGetArg      (__wasi_lookupflags_t , dirflags)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (uint32_t             , path_len)
    m3ApiGetArg      (__wasi_oflags_t      , oflags)
    m3ApiGetArg      (__wasi_rights_t      , fs_rights_base)
    m3ApiGetArg      (__wasi_rights_t      , fs_rights_inheriting)
    m3ApiGetArg      (__wasi_fdflags_t     , fs_flags)
    m3ApiGetArgMem   (__wasi_fd_t *        , fd)

    __wasi_errno_t ret = __wasi_path_open(dirfd, dirflags,
                                          path, path_len,
                                          oflags, fs_rights_base, fs_rights_inheriting,
                                          fs_flags, fd);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_path_filestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_lookupflags_t , flags)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (uint32_t             , path_len)
    m3ApiGetArgMem   (__wasi_filestat_t *  , buf)

    __wasi_errno_t ret = __wasi_path_filestat_get(fd, flags, path, path_len, buf);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_read)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_iovec_t*      , wasi_iovs)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (__wasi_size_t*       , nread)

    __wasi_iovec_t iovs[iovs_len];
    copy_iov_to_host(_mem, iovs, wasi_iovs, iovs_len);

    __wasi_errno_t ret = __wasi_fd_read(fd, iovs, iovs_len, nread);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_write)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_iovec_t*      , wasi_iovs)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (__wasi_size_t*       , nwritten)

    __wasi_iovec_t iovs[iovs_len];
    copy_iov_to_host(_mem, iovs, wasi_iovs, iovs_len);

    __wasi_errno_t ret = __wasi_fd_write(fd, (__wasi_ciovec_t*)iovs, iovs_len, nwritten);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_close)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t, fd)

    __wasi_errno_t ret = __wasi_fd_close(fd);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_datasync)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t, fd)

    __wasi_errno_t ret = __wasi_fd_datasync(fd);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_random_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint8_t*             , buf)
    m3ApiGetArg      (__wasi_size_t        , buflen)

    __wasi_errno_t ret = __wasi_random_get(buf, buflen);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_clock_res_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_clockid_t     , wasi_clk_id)
    m3ApiGetArgMem   (__wasi_timestamp_t*  , resolution)

    __wasi_errno_t ret = __wasi_clock_res_get(wasi_clk_id, resolution);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_clock_time_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_clockid_t     , wasi_clk_id)
    m3ApiGetArg      (__wasi_timestamp_t   , precision)
    m3ApiGetArgMem   (__wasi_timestamp_t*  , time)

    __wasi_errno_t ret = __wasi_clock_time_get(wasi_clk_id, precision, time);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_poll_oneoff)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (const __wasi_subscription_t*  , in)
    m3ApiGetArgMem   (__wasi_event_t*               , out)
    m3ApiGetArg      (__wasi_size_t                 , nsubscriptions)
    m3ApiGetArgMem   (__wasi_size_t*                , nevents)

    __wasi_errno_t ret = __wasi_poll_oneoff(in, out, nsubscriptions, nevents);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_proc_exit)
{
    m3ApiGetArg      (uint32_t, code)

    runtime->exit_code = code;

    m3ApiTrap(m3Err_trapExit);
}


static
M3Result SuppressLookupFailure(M3Result i_result)
{
    if (i_result == m3Err_functionLookupFailed)
        return m3Err_none;
    else
        return i_result;
}


M3Result  m3_LinkWASI  (IM3Module module)
{
    M3Result result = m3Err_none;

    const char* wasi  = "wasi_unstable";

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "args_sizes_get",       "i(**)",   &m3_wasi_unstable_args_sizes_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_sizes_get",    "i(**)",   &m3_wasi_unstable_environ_sizes_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "args_get",             "i(**)",   &m3_wasi_unstable_args_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_get",          "i(**)",   &m3_wasi_unstable_environ_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_dir_name",  "i(i*i)",  &m3_wasi_unstable_fd_prestat_dir_name)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_get",       "i(i*)",   &m3_wasi_unstable_fd_prestat_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_open",      "i(ii*iiIIi*)",  &m3_wasi_unstable_path_open)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_filestat_get",    "i(ii*i*)",  &m3_wasi_unstable_path_filestat_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_fdstat_get",        "i(i*)",   &m3_wasi_unstable_fd_fdstat_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_fdstat_set_flags",  "i(ii)",   &m3_wasi_unstable_fd_fdstat_set_flags)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_write",             "i(i*i*)", &m3_wasi_unstable_fd_write)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_read",              "i(i*i*)", &m3_wasi_unstable_fd_read)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_seek",              "i(iIi*)", &m3_wasi_unstable_fd_seek)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_datasync",          "i(i)",    &m3_wasi_unstable_fd_datasync)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_close",             "i(i)",    &m3_wasi_unstable_fd_close)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "random_get",           "i(*i)",   &m3_wasi_unstable_random_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "clock_res_get",        "i(i*)",   &m3_wasi_unstable_clock_res_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "clock_time_get",       "i(iI*)",  &m3_wasi_unstable_clock_time_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "poll_oneoff",          "i(**i*)", &m3_wasi_unstable_poll_oneoff)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "proc_exit",            "v(i)",    &m3_wasi_unstable_proc_exit)));

_catch:
    return result;
}

#endif // d_m3HasMetaWASI

