//
//  m3_api_uvwasi.c
//
//  Created by Colin J. Ihrig on 4/20/20.
//  Copyright © 2020 Colin J. Ihrig. All rights reserved.

//

#define _POSIX_C_SOURCE 200809L

#include "m3_api_wasi.h"

#include "m3_api_defs.h"
#include "m3_env.h"
#include "m3_exception.h"

#if defined(d_m3HasUVWASI)

typedef uint32_t __wasi_size_t;
#include "extra/wasi_core.h"

#include <stdio.h>
#include <string.h>

#include "uvwasi.h"

#ifdef __APPLE__
# include <crt_externs.h>
# define environ (*_NSGetEnviron())
#elif !defined(_MSC_VER)
extern char** environ;
#endif

uvwasi_t uvwasi;

typedef struct wasi_iovec_t
{
    __wasi_size_t buf;
    __wasi_size_t buf_len;
} wasi_iovec_t;

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

    m3ApiReturn(UVWASI_ESUCCESS);
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
    m3ApiReturn(UVWASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_environ_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (u32*                 , env)
    m3ApiGetArgMem   (char*                , env_buf)

    char **environment;
    uvwasi_errno_t err;

    environment = calloc(uvwasi.envc, sizeof(char *));
    if (environment == NULL) {
        m3ApiReturn(UVWASI_ENOMEM);
    }

    err = uvwasi_environ_get(&uvwasi, environment, env_buf);
    if (err != UVWASI_ESUCCESS) {
        free(environment);
        m3ApiReturn(err);
    }

    uint32_t environ_buf_offset = m3ApiPtrToOffset(env_buf);

    for (u32 i = 0; i < uvwasi.envc; ++i)
    {
        uint32_t offset = environ_buf_offset +
                          (environment[i] - environment[0]);
        env[i] = offset;
    }

    free(environment);
    m3ApiReturn(UVWASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_environ_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (__wasi_size_t*       , env_count)
    m3ApiGetArgMem   (__wasi_size_t*       , env_buf_size)

    size_t count;
    size_t buf_size;
    uvwasi_errno_t err;

    err = uvwasi_environ_sizes_get(&uvwasi, &count, &buf_size);
    *env_count = count;
    *env_buf_size = buf_size;
    m3ApiReturn(err);
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_dir_name)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (char*                , path)
    m3ApiGetArg      (size_t               , path_len)

    m3ApiReturn(uvwasi_fd_prestat_dir_name(&uvwasi, fd, path, path_len));
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (uint32_t *           , buf)

    uvwasi_prestat_t prestat;
    uvwasi_errno_t err;

    err = uvwasi_fd_prestat_get(&uvwasi, fd, &prestat);
    if (err != UVWASI_ESUCCESS) {
        m3ApiReturn(err);
    }

    // TODO(cjihrig): This memory writing logic is wrong.
    *buf = prestat.pr_type;
    *(buf + 4) = prestat.u.dir.pr_name_len;
    m3ApiReturn(UVWASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_fdstat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (uvwasi_fdstat_t*     , fdstat)

    m3ApiReturn(uvwasi_fd_fdstat_get(&uvwasi, fd, fdstat));
}

m3ApiRawFunction(m3_wasi_unstable_fd_fdstat_set_flags)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArg      (uvwasi_fdflags_t     , flags)

    m3ApiReturn(uvwasi_fd_fdstat_set_flags(&uvwasi, fd, flags));
}

m3ApiRawFunction(m3_wasi_unstable_fd_seek)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArg      (uvwasi_filedelta_t   , offset)
    m3ApiGetArg      (uvwasi_whence_t      , whence)
    m3ApiGetArgMem   (uvwasi_filesize_t*   , result)

    m3ApiReturn(uvwasi_fd_seek(&uvwasi, fd, offset, whence, result));
}


m3ApiRawFunction(m3_wasi_unstable_path_open)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , dirfd)
    m3ApiGetArg      (uvwasi_lookupflags_t , dirflags)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (__wasi_size_t        , path_len)
    m3ApiGetArg      (uvwasi_oflags_t      , oflags)
    m3ApiGetArg      (uvwasi_rights_t      , fs_rights_base)
    m3ApiGetArg      (uvwasi_rights_t      , fs_rights_inheriting)
    m3ApiGetArg      (uvwasi_fdflags_t     , fs_flags)
    m3ApiGetArgMem   (uvwasi_fd_t *        , fd)

    m3ApiReturn(uvwasi_path_open(&uvwasi,
                                 dirfd,
                                 dirflags,
                                 path,
                                 path_len,
                                 oflags,
                                 fs_rights_base,
                                 fs_rights_inheriting,
                                 fs_flags,
                                 fd));
}

m3ApiRawFunction(m3_wasi_unstable_fd_read)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (wasi_iovec_t*        , wasi_iovs)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (__wasi_size_t*       , nread)

    uvwasi_iovec_t* iovs = calloc(iovs_len, sizeof(uvwasi_iovec_t));
    size_t num_read;
    uvwasi_errno_t err;

    if (iovs == NULL) {
        m3ApiReturn(UVWASI_ENOMEM);
    }

    for (__wasi_size_t i = 0; i < iovs_len; ++i) {
        iovs[i].buf = m3ApiOffsetToPtr(wasi_iovs[i].buf);
        iovs[i].buf_len = wasi_iovs[i].buf_len;
    }

    err = uvwasi_fd_read(&uvwasi, fd, iovs, iovs_len, &num_read);
    *nread = num_read;
    free(iovs);
    m3ApiReturn(err);
}

m3ApiRawFunction(m3_wasi_unstable_fd_write)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t          , fd)
    m3ApiGetArgMem   (wasi_iovec_t*        , wasi_iovs)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (__wasi_size_t*       , nwritten)

    uvwasi_ciovec_t* iovs = calloc(iovs_len, sizeof(uvwasi_ciovec_t));
    size_t num_written;
    uvwasi_errno_t err;

    if (iovs == NULL) {
        m3ApiReturn(UVWASI_ENOMEM);
    }

    for (__wasi_size_t i = 0; i < iovs_len; ++i) {
        iovs[i].buf = m3ApiOffsetToPtr(wasi_iovs[i].buf);
        iovs[i].buf_len = wasi_iovs[i].buf_len;
    }

    err = uvwasi_fd_write(&uvwasi, fd, iovs, iovs_len, &num_written);
    *nwritten = num_written;
    free(iovs);
    m3ApiReturn(err);
}

m3ApiRawFunction(m3_wasi_unstable_fd_close)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t, fd)

    m3ApiReturn(uvwasi_fd_close(&uvwasi, fd));
}

m3ApiRawFunction(m3_wasi_unstable_fd_datasync)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_fd_t, fd)

    m3ApiReturn(uvwasi_fd_datasync(&uvwasi, fd));
}

m3ApiRawFunction(m3_wasi_unstable_random_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint8_t*             , buf)
    m3ApiGetArg      (__wasi_size_t        , buflen)

    m3ApiReturn(uvwasi_random_get(&uvwasi, buf, buflen));
}

m3ApiRawFunction(m3_wasi_unstable_clock_res_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_clockid_t     , wasi_clk_id)
    m3ApiGetArgMem   (uvwasi_timestamp_t*  , resolution)

    m3ApiReturn(uvwasi_clock_res_get(&uvwasi, wasi_clk_id, resolution));
}

m3ApiRawFunction(m3_wasi_unstable_clock_time_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uvwasi_clockid_t     , wasi_clk_id)
    m3ApiGetArg      (uvwasi_timestamp_t   , precision)
    m3ApiGetArgMem   (uvwasi_timestamp_t*  , time)

    m3ApiReturn(uvwasi_clock_time_get(&uvwasi, wasi_clk_id, precision, time));
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

    // TODO(cjihrig): uvwasi currently implements 'wasi_snapshot_preview1' but
    // call it 'wasi_unstable' here for compatibility with the other wasm3 WASI
    // implementations.
    const char* wasi  = "wasi_unstable";

    uvwasi_options_t init_options;
    uvwasi_errno_t err;

    init_options.in = 0;
    init_options.out = 1;
    init_options.err = 2;
    init_options.fd_table_size = 3;
    // runtime->argc is not initialized at this point. However, it's fine to
    // use the runtime instead of uvwasi to implement the two WASI functions for
    // working with command line arguments.
    init_options.argc = 0;
    init_options.argv = NULL;
    init_options.envp = (char**) environ;
    init_options.preopenc = 1;
    // TODO(cjihrig): This requires better support for the --dir command line
    // flag to implement properly. For now, just let WASI applications access
    // the current working directory as the sandboxed root directory.
    init_options.preopens = calloc(1, sizeof(uvwasi_preopen_t));
    if (init_options.preopens == NULL) {
        result = m3Err_mallocFailed;
        return result;
    }

    init_options.preopens[0].mapped_path = "/";
    init_options.preopens[0].real_path = ".";
    init_options.allocator = NULL;
    err = uvwasi_init(&uvwasi, &init_options);
    free(init_options.preopens);

    // uvwasi_init() returns WASI errors, which don't really map to m3 Errors,
    // so return unknown error for now.
    if (err != UVWASI_ESUCCESS) {
        result = m3Err_unknownError;
        return result;
    }

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "args_sizes_get",       "i(**)",   &m3_wasi_unstable_args_sizes_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_sizes_get",    "i(**)",   &m3_wasi_unstable_environ_sizes_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "args_get",             "i(**)",   &m3_wasi_unstable_args_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_get",          "i(**)",   &m3_wasi_unstable_environ_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_dir_name",  "i(i*i)",  &m3_wasi_unstable_fd_prestat_dir_name)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_get",       "i(i*)",   &m3_wasi_unstable_fd_prestat_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_open",            "i(ii*iiIIi*)",  &m3_wasi_unstable_path_open)));

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
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "proc_exit",            "v(i)",    &m3_wasi_unstable_proc_exit)));

_catch:
    return result;
}

#endif // d_m3HasUVWASI

