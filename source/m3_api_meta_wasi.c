//
//  m3_api_meta_wasi.c
//
//  Created by Volodymyr Shymanskyy on 01/08/20.
//  Copyright © 2020 Volodymyr Shymanskyy. All rights reserved.
//

#include "m3_api_wasi.h"

#include "m3_env.h"
#include "m3_exception.h"

#if defined(d_m3HasMetaWASI)

// NOTE: MetaWASI mostly redirects WASI calls to the host WASI environment

#if !defined(__wasi__)
# error "MetaWASI is only supported on WASI target"
#endif

#if __has_include("wasi/api.h")
# include <wasi/api.h>

#elif __has_include("wasi/core.h")
# warning "Using legacy WASI headers"
# include <wasi/core.h>
# define __WASI_ERRNO_SUCCESS   __WASI_ESUCCESS
# define __WASI_ERRNO_INVAL     __WASI_EINVAL

#else
# error "Missing WASI headers"
#endif

static m3_wasi_context_t* wasi_context;

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

m3ApiRawFunction(m3_wasi_generic_args_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t *           , argv)
    m3ApiGetArgMem   (char *               , argv_buf)

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) { m3ApiReturn(__WASI_ERRNO_INVAL); }

    m3ApiCheckMem(argv, context->argc * sizeof(uint32_t));

    for (u32 i = 0; i < context->argc; ++i)
    {
        m3ApiWriteMem32(&argv[i], m3ApiPtrToOffset(argv_buf));

        size_t len = strlen (context->argv[i]);

        m3ApiCheckMem(argv_buf, len);
        memcpy (argv_buf, context->argv[i], len);
        argv_buf += len;
        * argv_buf++ = 0;
    }

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_args_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (__wasi_size_t *      , argc)
    m3ApiGetArgMem   (__wasi_size_t *      , argv_buf_size)

    m3ApiCheckMem(argc,             sizeof(__wasi_size_t));
    m3ApiCheckMem(argv_buf_size,    sizeof(__wasi_size_t));

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) { m3ApiReturn(__WASI_ERRNO_INVAL); }

    __wasi_size_t buf_len = 0;
    for (u32 i = 0; i < context->argc; ++i)
    {
        buf_len += strlen (context->argv[i]) + 1;
    }

    m3ApiWriteMem32(argc, context->argc);
    m3ApiWriteMem32(argv_buf_size, buf_len);

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_environ_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t *           , env)
    m3ApiGetArgMem   (char *               , env_buf)

    __wasi_errno_t ret;
    __wasi_size_t env_count, env_buf_size;

    ret = __wasi_environ_sizes_get(&env_count, &env_buf_size);
    if (ret != __WASI_ERRNO_SUCCESS) m3ApiReturn(ret);

    m3ApiCheckMem(env,      env_count * sizeof(uint32_t));
    m3ApiCheckMem(env_buf,  env_buf_size);

    ret = __wasi_environ_get(env, env_buf);
    if (ret != __WASI_ERRNO_SUCCESS) m3ApiReturn(ret);

    for (u32 i = 0; i < env_count; ++i) {
        env[i] = m3ApiPtrToOffset (env[i]);
    }

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_environ_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (__wasi_size_t *      , env_count)
    m3ApiGetArgMem   (__wasi_size_t *      , env_buf_size)

    m3ApiCheckMem(env_count,    sizeof(__wasi_size_t));
    m3ApiCheckMem(env_buf_size, sizeof(__wasi_size_t));

    __wasi_errno_t ret = __wasi_environ_sizes_get(env_count, env_buf_size);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_prestat_dir_name)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (char *               , path)
    m3ApiGetArg      (__wasi_size_t        , path_len)

    m3ApiCheckMem(path, path_len);

    __wasi_errno_t ret = __wasi_fd_prestat_dir_name(fd, path, path_len);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_prestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_prestat_t *   , buf)

    m3ApiCheckMem(buf, sizeof(__wasi_prestat_t));

    __wasi_errno_t ret = __wasi_fd_prestat_get(fd, buf);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_fdstat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_fdstat_t *    , fdstat)

    m3ApiCheckMem(fdstat, sizeof(__wasi_fdstat_t));

    __wasi_errno_t ret = __wasi_fd_fdstat_get(fd, fdstat);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_fdstat_set_flags)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_fdflags_t     , flags)

    __wasi_errno_t ret = __wasi_fd_fdstat_set_flags(fd, flags);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_filestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (uint8_t *            , buf)

    m3ApiCheckMem(buf, 56); // wasi_filestat_t

    __wasi_filestat_t stat;

    __wasi_errno_t ret = __wasi_fd_filestat_get(fd, &stat);

    if (ret != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(ret);
    }

    memset(buf, 0, 56);
    m3ApiWriteMem64(buf+0,  stat.st_dev);
    m3ApiWriteMem64(buf+8,  stat.st_ino);
    m3ApiWriteMem8 (buf+16, stat.st_filetype);
    m3ApiWriteMem32(buf+20, stat.st_nlink);
    m3ApiWriteMem64(buf+24, stat.st_size);
    m3ApiWriteMem64(buf+32, stat.st_atim);
    m3ApiWriteMem64(buf+40, stat.st_mtim);
    m3ApiWriteMem64(buf+48, stat.st_ctim);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_snapshot_preview1_fd_filestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (uint8_t *            , buf)

    m3ApiCheckMem(buf, 64); // wasi_filestat_t

    __wasi_filestat_t stat;

    __wasi_errno_t ret = __wasi_fd_filestat_get(fd, &stat);

    if (ret != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(ret);
    }

    memset(buf, 0, 64);
    m3ApiWriteMem64(buf+0,  stat.st_dev);
    m3ApiWriteMem64(buf+8,  stat.st_ino);
    m3ApiWriteMem8 (buf+16, stat.st_filetype);
    m3ApiWriteMem64(buf+24, stat.st_nlink);
    m3ApiWriteMem64(buf+32, stat.st_size);
    m3ApiWriteMem64(buf+40, stat.st_atim);
    m3ApiWriteMem64(buf+48, stat.st_mtim);
    m3ApiWriteMem64(buf+56, stat.st_ctim);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_seek)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_filedelta_t   , offset)
    m3ApiGetArg      (uint32_t             , wasi_whence)
    m3ApiGetArgMem   (__wasi_filesize_t *  , result)

    m3ApiCheckMem(result, sizeof(__wasi_filesize_t));

    __wasi_whence_t whence;

    switch (wasi_whence) {
    case 0: whence = __WASI_WHENCE_CUR; break;
    case 1: whence = __WASI_WHENCE_END; break;
    case 2: whence = __WASI_WHENCE_SET; break;
    default:                m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    __wasi_errno_t ret = __wasi_fd_seek(fd, offset, whence, result);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_snapshot_preview1_fd_seek)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_filedelta_t   , offset)
    m3ApiGetArg      (uint32_t             , wasi_whence)
    m3ApiGetArgMem   (__wasi_filesize_t *  , result)

    m3ApiCheckMem(result, sizeof(__wasi_filesize_t));

    __wasi_whence_t whence;

    switch (wasi_whence) {
    case 0: whence = __WASI_WHENCE_SET; break;
    case 1: whence = __WASI_WHENCE_CUR; break;
    case 2: whence = __WASI_WHENCE_END; break;
    default:                m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    __wasi_errno_t ret = __wasi_fd_seek(fd, offset, whence, result);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_path_create_directory)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (__wasi_size_t        , path_len)

    m3ApiCheckMem(path, path_len);

    __wasi_errno_t ret = __wasi_path_create_directory(fd, path, path_len);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_path_readlink)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (__wasi_size_t        , path_len)
    m3ApiGetArgMem   (char *               , buf)
    m3ApiGetArg      (__wasi_size_t        , buf_len)
    m3ApiGetArgMem   (__wasi_size_t *      , bufused)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(buf, buf_len);
    m3ApiCheckMem(bufused, sizeof(__wasi_size_t));

    __wasi_errno_t ret = __wasi_path_readlink(fd, path, path_len, buf, buf_len, bufused);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_path_remove_directory)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (__wasi_size_t        , path_len)

    m3ApiCheckMem(path, path_len);

    __wasi_errno_t ret = __wasi_path_remove_directory(fd, path, path_len);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_path_rename)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , old_fd)
    m3ApiGetArgMem   (const char *         , old_path)
    m3ApiGetArg      (__wasi_size_t        , old_path_len)
    m3ApiGetArg      (__wasi_fd_t          , new_fd)
    m3ApiGetArgMem   (const char *         , new_path)
    m3ApiGetArg      (__wasi_size_t        , new_path_len)

    m3ApiCheckMem(old_path, old_path_len);
    m3ApiCheckMem(new_path, new_path_len);

    __wasi_errno_t ret = __wasi_path_rename(old_fd, old_path, old_path_len,
                                            new_fd, new_path, new_path_len);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_path_unlink_file)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (__wasi_size_t        , path_len)

    m3ApiCheckMem(path, path_len);

    __wasi_errno_t ret = __wasi_path_unlink_file(fd, path, path_len);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_path_open)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , dirfd)
    m3ApiGetArg      (__wasi_lookupflags_t , dirflags)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (__wasi_size_t        , path_len)
    m3ApiGetArg      (__wasi_oflags_t      , oflags)
    m3ApiGetArg      (__wasi_rights_t      , fs_rights_base)
    m3ApiGetArg      (__wasi_rights_t      , fs_rights_inheriting)
    m3ApiGetArg      (__wasi_fdflags_t     , fs_flags)
    m3ApiGetArgMem   (__wasi_fd_t *        , fd)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(fd,   sizeof(__wasi_fd_t));

    __wasi_errno_t ret = __wasi_path_open(dirfd,
                                 dirflags,
                                 path,
                                 path_len,
                                 oflags,
                                 fs_rights_base,
                                 fs_rights_inheriting,
                                 fs_flags,
                                 fd);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_unstable_path_filestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_lookupflags_t , flags)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (uint32_t             , path_len)
    m3ApiGetArgMem   (uint8_t *            , buf)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(buf,  56); // wasi_filestat_t

    __wasi_filestat_t stat;

    __wasi_errno_t ret = __wasi_path_filestat_get(fd, flags, path, path_len, &stat);

    if (ret != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(ret);
    }

    memset(buf, 0, 56);
    m3ApiWriteMem64(buf+0,  stat.st_dev);
    m3ApiWriteMem64(buf+8,  stat.st_ino);
    m3ApiWriteMem8 (buf+16, stat.st_filetype);
    m3ApiWriteMem32(buf+20, stat.st_nlink);
    m3ApiWriteMem64(buf+24, stat.st_size);
    m3ApiWriteMem64(buf+32, stat.st_atim);
    m3ApiWriteMem64(buf+40, stat.st_mtim);
    m3ApiWriteMem64(buf+48, stat.st_ctim);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_snapshot_preview1_path_filestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_lookupflags_t , flags)
    m3ApiGetArgMem   (const char *         , path)
    m3ApiGetArg      (uint32_t             , path_len)
    m3ApiGetArgMem   (uint8_t *            , buf)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(buf,  64); // wasi_filestat_t

    __wasi_filestat_t stat;

    __wasi_errno_t ret = __wasi_path_filestat_get(fd, flags, path, path_len, &stat);

    if (ret != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(ret);
    }

    memset(buf, 0, 64);
    m3ApiWriteMem64(buf+0,  stat.st_dev);
    m3ApiWriteMem64(buf+8,  stat.st_ino);
    m3ApiWriteMem8 (buf+16, stat.st_filetype);
    m3ApiWriteMem64(buf+24, stat.st_nlink);
    m3ApiWriteMem64(buf+32, stat.st_size);
    m3ApiWriteMem64(buf+40, stat.st_atim);
    m3ApiWriteMem64(buf+48, stat.st_mtim);
    m3ApiWriteMem64(buf+56, stat.st_ctim);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_pread)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_iovec_t *     , wasi_iovs)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArg      (__wasi_filesize_t    , offset)
    m3ApiGetArgMem   (__wasi_size_t *      , nread)

    m3ApiCheckMem(wasi_iovs,    iovs_len * sizeof(__wasi_iovec_t));
    m3ApiCheckMem(nread,        sizeof(__wasi_size_t));

    __wasi_iovec_t iovs[iovs_len];
    copy_iov_to_host(_mem, iovs, wasi_iovs, iovs_len);

    __wasi_errno_t ret = __wasi_fd_pread(fd, iovs, iovs_len, offset, nread);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_read)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_iovec_t *     , wasi_iovs)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (__wasi_size_t *      , nread)

    m3ApiCheckMem(wasi_iovs,    iovs_len * sizeof(__wasi_iovec_t));
    m3ApiCheckMem(nread,        sizeof(__wasi_size_t));

    __wasi_iovec_t iovs[iovs_len];
    copy_iov_to_host(_mem, iovs, wasi_iovs, iovs_len);

    __wasi_errno_t ret = __wasi_fd_read(fd, iovs, iovs_len, nread);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_write)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_iovec_t *     , wasi_iovs)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (__wasi_size_t *      , nwritten)

    m3ApiCheckMem(wasi_iovs,    iovs_len * sizeof(__wasi_iovec_t));
    m3ApiCheckMem(nwritten,     sizeof(__wasi_size_t));

    __wasi_iovec_t iovs[iovs_len];
    copy_iov_to_host(_mem, iovs, wasi_iovs, iovs_len);

    __wasi_errno_t ret = __wasi_fd_write(fd, (__wasi_ciovec_t*)iovs, iovs_len, nwritten);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_readdir)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (void *               , buf)
    m3ApiGetArg      (__wasi_size_t        , buf_len)
    m3ApiGetArg      (__wasi_dircookie_t   , cookie)
    m3ApiGetArgMem   (__wasi_size_t *      , bufused)

    m3ApiCheckMem(buf,      buf_len);
    m3ApiCheckMem(bufused,  sizeof(__wasi_size_t));

    __wasi_errno_t ret = __wasi_fd_readdir(fd, buf, buf_len, cookie, bufused);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_close)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t, fd)

    __wasi_errno_t ret = __wasi_fd_close(fd);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_datasync)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t, fd)

    __wasi_errno_t ret = __wasi_fd_datasync(fd);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_random_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint8_t *            , buf)
    m3ApiGetArg      (__wasi_size_t        , buf_len)

    m3ApiCheckMem(buf, buf_len);

    __wasi_errno_t ret = __wasi_random_get(buf, buf_len);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_clock_res_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_clockid_t     , wasi_clk_id)
    m3ApiGetArgMem   (__wasi_timestamp_t * , resolution)

    m3ApiCheckMem(resolution, sizeof(__wasi_timestamp_t));

    __wasi_errno_t ret = __wasi_clock_res_get(wasi_clk_id, resolution);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_clock_time_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_clockid_t     , wasi_clk_id)
    m3ApiGetArg      (__wasi_timestamp_t   , precision)
    m3ApiGetArgMem   (__wasi_timestamp_t * , time)

    m3ApiCheckMem(time, sizeof(__wasi_timestamp_t));

    __wasi_errno_t ret = __wasi_clock_time_get(wasi_clk_id, precision, time);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_poll_oneoff)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (const __wasi_subscription_t * , in)
    m3ApiGetArgMem   (__wasi_event_t *              , out)
    m3ApiGetArg      (__wasi_size_t                 , nsubscriptions)
    m3ApiGetArgMem   (__wasi_size_t *               , nevents)

    m3ApiCheckMem(in,       nsubscriptions * sizeof(__wasi_subscription_t));
    m3ApiCheckMem(out,      nsubscriptions * sizeof(__wasi_event_t));
    m3ApiCheckMem(nevents,  sizeof(__wasi_size_t));

    __wasi_errno_t ret = __wasi_poll_oneoff(in, out, nsubscriptions, nevents);

    m3ApiReturn(ret);
}

m3ApiRawFunction(m3_wasi_generic_proc_exit)
{
    m3ApiGetArg      (uint32_t, code)

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context) {
        context->exit_code = code;
    }

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

m3_wasi_context_t* m3_GetWasiContext()
{
    return wasi_context;
}


M3Result  m3_LinkWASI  (IM3Module module)
{
    M3Result result = m3Err_none;

    if (!wasi_context) {
        wasi_context = (m3_wasi_context_t*)malloc(sizeof(m3_wasi_context_t));
        wasi_context->exit_code = 0;
        wasi_context->argc = 0;
        wasi_context->argv = 0;
    }

    static const char* namespaces[2] = { "wasi_unstable", "wasi_snapshot_preview1" };

    // fd_seek is incompatible
_   (SuppressLookupFailure (m3_LinkRawFunction (module, "wasi_unstable",          "fd_seek",           "i(iIi*)",   &m3_wasi_unstable_fd_seek)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, "wasi_snapshot_preview1", "fd_seek",           "i(iIi*)",   &m3_wasi_snapshot_preview1_fd_seek)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, "wasi_unstable",          "fd_filestat_get",   "i(i*)",     &m3_wasi_unstable_fd_filestat_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, "wasi_snapshot_preview1", "fd_filestat_get",   "i(i*)",     &m3_wasi_snapshot_preview1_fd_filestat_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, "wasi_unstable",          "path_filestat_get", "i(ii*i*)",  &m3_wasi_unstable_path_filestat_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, "wasi_snapshot_preview1", "path_filestat_get", "i(ii*i*)",  &m3_wasi_snapshot_preview1_path_filestat_get)));

    for (int i=0; i<2; i++)
    {
        const char* wasi = namespaces[i];

_       (SuppressLookupFailure (m3_LinkRawFunctionEx (module, wasi, "args_get",           "i(**)",   &m3_wasi_generic_args_get, wasi_context)));
_       (SuppressLookupFailure (m3_LinkRawFunctionEx (module, wasi, "args_sizes_get",     "i(**)",   &m3_wasi_generic_args_sizes_get, wasi_context)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "clock_res_get",        "i(i*)",   &m3_wasi_generic_clock_res_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "clock_time_get",       "i(iI*)",  &m3_wasi_generic_clock_time_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_get",          "i(**)",   &m3_wasi_generic_environ_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_sizes_get",    "i(**)",   &m3_wasi_generic_environ_sizes_get)));

//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_advise",            "i(iIIi)", )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_allocate",          "i(iII)",  )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_close",             "i(i)",    &m3_wasi_generic_fd_close)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_datasync",          "i(i)",    &m3_wasi_generic_fd_datasync)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_fdstat_get",        "i(i*)",   &m3_wasi_generic_fd_fdstat_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_fdstat_set_flags",  "i(ii)",   &m3_wasi_generic_fd_fdstat_set_flags)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_fdstat_set_rights", "i(iII)",  )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_filestat_set_size", "i(iI)",   )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_filestat_set_times","i(iIIi)", )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_pread",             "i(i*iI*)",&m3_wasi_generic_fd_pread)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_get",       "i(i*)",   &m3_wasi_generic_fd_prestat_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_dir_name",  "i(i*i)",  &m3_wasi_generic_fd_prestat_dir_name)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_pwrite",            "i(i*iI*)",)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_read",              "i(i*i*)", &m3_wasi_generic_fd_read)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_readdir",           "i(i*iI*)",&m3_wasi_generic_fd_readdir)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_renumber",          "i(ii)",   )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_sync",              "i(i)",    )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_tell",              "i(i*)",   )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_write",             "i(i*i*)", &m3_wasi_generic_fd_write)));

_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_create_directory",    "i(i*i)",       &m3_wasi_generic_path_create_directory)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_filestat_set_times",  "i(ii*iIIi)",   )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_link",                "i(ii*ii*i)",   )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_open",                "i(ii*iiIIi*)", &m3_wasi_generic_path_open)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_readlink",            "i(i*i*i*)",    &m3_wasi_generic_path_readlink)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_remove_directory",    "i(i*i)",       &m3_wasi_generic_path_remove_directory)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_rename",              "i(i*ii*i)",    &m3_wasi_generic_path_rename)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_symlink",             "i(*ii*i)",     )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_unlink_file",         "i(i*i)",       &m3_wasi_generic_path_unlink_file)));

_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "poll_oneoff",          "i(**i*)", &m3_wasi_generic_poll_oneoff)));
_       (SuppressLookupFailure (m3_LinkRawFunctionEx (module, wasi, "proc_exit",          "v(i)",    &m3_wasi_generic_proc_exit, wasi_context)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "proc_raise",           "i(i)",    )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "random_get",           "i(*i)",   &m3_wasi_generic_random_get)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sched_yield",          "i()",     )));

//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sock_recv",            "i(i*ii**)",        )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sock_send",            "i(i*ii*)",         )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sock_shutdown",        "i(ii)",            )));
    }

_catch:
    return result;
}

#endif // d_m3HasMetaWASI

