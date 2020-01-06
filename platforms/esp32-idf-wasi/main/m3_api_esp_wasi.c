//
//  m3_api_esp_wasi.c
//
//  Created by Volodymyr Shymanskyy on 01/07/20.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#define _POSIX_C_SOURCE 200809L

#include "m3_api_esp_wasi.h"

#include "m3/m3_api_defs.h"
#include "m3/m3_env.h"
#include "m3/m3_module.h"
#include "m3/m3_exception.h"

#if defined(ESP32)

typedef uint32_t __wasi_size_t;
#include "m3/extra/wasi_core.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

struct wasi_iovec
{
    __wasi_size_t iov_base;
    __wasi_size_t iov_len;
};

#define PREOPEN_CNT   3

typedef struct Preopen {
    int         fd;
    char*       path;
} Preopen;

Preopen preopen[PREOPEN_CNT] = {
    {  0, "<stdin>"  },
    {  1, "<stdout>" },
    {  2, "<stderr>" },
};

static
__wasi_errno_t errno_to_wasi(int errnum) {
    switch (errnum) {
    case EPERM:   return __WASI_EPERM;   break;
    case ENOENT:  return __WASI_ENOENT;  break;
    case ESRCH:   return __WASI_ESRCH;   break;
    case EINTR:   return __WASI_EINTR;   break;
    case EIO:     return __WASI_EIO;     break;
    case ENXIO:   return __WASI_ENXIO;   break;
    case E2BIG:   return __WASI_E2BIG;   break;
    case ENOEXEC: return __WASI_ENOEXEC; break;
    case EBADF:   return __WASI_EBADF;   break;
    case ECHILD:  return __WASI_ECHILD;  break;
    case EAGAIN:  return __WASI_EAGAIN;  break;
    case ENOMEM:  return __WASI_ENOMEM;  break;
    case EACCES:  return __WASI_EACCES;  break;
    case EFAULT:  return __WASI_EFAULT;  break;
    case EBUSY:   return __WASI_EBUSY;   break;
    case EEXIST:  return __WASI_EEXIST;  break;
    case EXDEV:   return __WASI_EXDEV;   break;
    case ENODEV:  return __WASI_ENODEV;  break;
    case ENOTDIR: return __WASI_ENOTDIR; break;
    case EISDIR:  return __WASI_EISDIR;  break;
    case EINVAL:  return __WASI_EINVAL;  break;
    case ENFILE:  return __WASI_ENFILE;  break;
    case EMFILE:  return __WASI_EMFILE;  break;
    case ENOTTY:  return __WASI_ENOTTY;  break;
    case ETXTBSY: return __WASI_ETXTBSY; break;
    case EFBIG:   return __WASI_EFBIG;   break;
    case ENOSPC:  return __WASI_ENOSPC;  break;
    case ESPIPE:  return __WASI_ESPIPE;  break;
    case EROFS:   return __WASI_EROFS;   break;
    case EMLINK:  return __WASI_EMLINK;  break;
    case EPIPE:   return __WASI_EPIPE;   break;
    case EDOM:    return __WASI_EDOM;    break;
    case ERANGE:  return __WASI_ERANGE;  break;
    default:      return __WASI_EINVAL;
    }
}

static inline
int convert_clockid(__wasi_clockid_t in) {
    switch (in) {
    case __WASI_CLOCK_MONOTONIC:            return CLOCK_MONOTONIC;
    //case __WASI_CLOCK_PROCESS_CPUTIME_ID:   return CLOCK_PROCESS_CPUTIME_ID;
    case __WASI_CLOCK_REALTIME:             return CLOCK_REALTIME;
    //case __WASI_CLOCK_THREAD_CPUTIME_ID:    return CLOCK_THREAD_CPUTIME_ID;
    default: return -1;
    }
}

static inline
__wasi_timestamp_t convert_timespec(const struct timespec *ts) {
    if (ts->tv_sec < 0)
        return 0;
    if ((__wasi_timestamp_t)ts->tv_sec >= UINT64_MAX / 1000000000)
        return UINT64_MAX;
    return (__wasi_timestamp_t)ts->tv_sec * 1000000000 + ts->tv_nsec;
}


/*
 * WASI API implementation
 */

m3ApiRawFunction(m3_wasi_unstable_args_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (u32 *                , argv_offset)
    m3ApiGetArgMem   (u8 *                 , argv_buf_offset)

    if (runtime == NULL) { m3ApiReturn(__WASI_EINVAL); }

    for (u32 i = 0; i < runtime->argc; ++i)
    {
        argv_offset [i] = m3ApiPtrToOffset (argv_buf_offset);
        
        size_t len = strlen (runtime->argv [i]);
        memcpy (argv_buf_offset, runtime->argv [i], len);
        argv_buf_offset += len;
        * argv_buf_offset++ = 0;
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
    m3ApiGetArg      (uint32_t             , environ_ptrs_offset)
    m3ApiGetArg      (uint32_t             , environ_strs_offset)

    if (runtime == NULL) { m3ApiReturn(__WASI_EINVAL); }
    // TODO
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_environ_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (__wasi_size_t*       , environ_count)
    m3ApiGetArgMem   (__wasi_size_t*       , environ_buf_size)

    if (runtime == NULL) { m3ApiReturn(__WASI_EINVAL); }
    // TODO
    *environ_count = 0;
    *environ_buf_size = 0;
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_dir_name)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (char*                , path)
    m3ApiGetArg      (__wasi_size_t        , path_len)

    if (runtime == NULL) { m3ApiReturn(__WASI_EINVAL); }
    if (fd < 3 || fd >= PREOPEN_CNT) { m3ApiReturn(__WASI_EBADF); }
    int size = min(strlen(preopen[fd].path), path_len);
    memcpy(path, preopen[fd].path, size);
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (uint32_t*            , buf)  // TODO: use actual struct

    if (runtime == NULL) { m3ApiReturn(__WASI_EINVAL); }
    if (fd < 3 || fd >= PREOPEN_CNT) { m3ApiReturn(__WASI_EBADF); }
    *(buf)   = __WASI_PREOPENTYPE_DIR;
    *(buf+1) = strlen(preopen[fd].path);
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_fdstat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (__wasi_fdstat_t*     , fdstat)

    if (runtime == NULL || fdstat == NULL) { m3ApiReturn(__WASI_EINVAL); }
    
    struct stat fd_stat;
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0) { m3ApiReturn(errno_to_wasi(errno)); }
    fstat(fd, &fd_stat);
    int mode = fd_stat.st_mode;
    fdstat->fs_filetype = (S_ISBLK(mode)   ? __WASI_FILETYPE_BLOCK_DEVICE     : 0) |
                          (S_ISCHR(mode)   ? __WASI_FILETYPE_CHARACTER_DEVICE : 0) |
                          (S_ISDIR(mode)   ? __WASI_FILETYPE_DIRECTORY        : 0) |
                          (S_ISREG(mode)   ? __WASI_FILETYPE_REGULAR_FILE     : 0) |
                          //(S_ISSOCK(mode)  ? __WASI_FILETYPE_SOCKET_STREAM    : 0) |
                          (S_ISLNK(mode)   ? __WASI_FILETYPE_SYMBOLIC_LINK    : 0);
    fdstat->fs_flags = ((fl & O_APPEND)    ? __WASI_FDFLAG_APPEND    : 0) |
                       //((fl & O_DSYNC)     ? __WASI_FDFLAG_DSYNC     : 0) |
                       ((fl & O_NONBLOCK)  ? __WASI_FDFLAG_NONBLOCK  : 0) |
                       //((fl & O_RSYNC)     ? __WASI_FDFLAG_RSYNC     : 0) |
                       ((fl & O_SYNC)      ? __WASI_FDFLAG_SYNC      : 0);
    fdstat->fs_rights_base = (uint64_t)-1; // all rights
    fdstat->fs_rights_inheriting = (uint64_t)-1; // all rights
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_seek)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_filedelta_t   , offset)
    m3ApiGetArg      (__wasi_whence_t      , whence)
    m3ApiGetArgMem   (__wasi_filesize_t*   , result)

    if (runtime == NULL || result == NULL) { m3ApiReturn(__WASI_EINVAL); }

    int wasi_whence = whence == __WASI_WHENCE_END ? SEEK_END :
                                __WASI_WHENCE_CUR ? SEEK_CUR : 0;
    int64_t ret;
    ret = lseek(fd, offset, wasi_whence);

    if (ret < 0) { m3ApiReturn(errno_to_wasi(errno)); }
    *result = ret;
    m3ApiReturn(__WASI_ESUCCESS);
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

    if (path_len >= 512)
        m3ApiReturn(__WASI_EINVAL);

    // copy path so we can ensure it is NULL terminated
    char host_path [path_len+1];

    memcpy (host_path, path, path_len);
    host_path [path_len] = '\0'; // NULL terminator

    // TODO
    m3ApiReturn(__WASI_ENOSYS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_read)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (uint32_t             , iovs_offset)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (__wasi_size_t*       , nread)

    if (runtime == NULL || nread == NULL) { m3ApiReturn(__WASI_EINVAL); }

    ssize_t res = 0;
    struct wasi_iovec *wasi_iov = m3ApiOffsetToPtr(iovs_offset);
    for (__wasi_size_t i = 0; i < iovs_len; i++) {
        void* addr = m3ApiOffsetToPtr(wasi_iov[i].iov_base);
        size_t len = wasi_iov[i].iov_len;
        if (len == 0) continue;

        int ret = read (fd, addr, len);
        if (ret < 0) m3ApiReturn(errno_to_wasi(errno));
        res += ret;
        if ((size_t)ret < len) break;
    }
    *nread = res;
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_write)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (uint32_t             , iovs_offset)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (__wasi_size_t*       , nwritten)

    if (runtime == NULL || nwritten == NULL) { m3ApiReturn(__WASI_EINVAL); }

    ssize_t res = 0;
    struct wasi_iovec *wasi_iov = m3ApiOffsetToPtr(iovs_offset);
    for (__wasi_size_t i = 0; i < iovs_len; i++) {
        void* addr = m3ApiOffsetToPtr(wasi_iov[i].iov_base);
        size_t len = wasi_iov[i].iov_len;
        if (len == 0) continue;

        int ret = write (fd, addr, len);
        if (ret < 0) m3ApiReturn(errno_to_wasi(errno));
        res += ret;
        if ((size_t)ret < len) break;
    }
    *nwritten = res;
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_close)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t, fd)

    int ret = close(fd);
    m3ApiReturn(ret == 0 ? __WASI_ESUCCESS : ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_datasync)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t, fd)

    // TODO
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_random_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint8_t*             , buf)
    m3ApiGetArg      (__wasi_size_t        , buflen)

    while (1) {
        ssize_t retlen = 0;

#if defined(__wasi__) || defined(__APPLE__) || defined(__ANDROID_API__) || defined(__OpenBSD__)
        size_t reqlen = min(buflen, 256);
        if (getentropy(buf, reqlen) < 0) {
            retlen = -1;
        } else {
            retlen = reqlen;
        }
#elif defined(__FreeBSD__) || defined(__linux__)
        retlen = getrandom(buf, buflen, 0);
#elif defined(_WIN32)
        if (RtlGenRandom(buf, buflen) == TRUE) {
            m3ApiReturn(__WASI_ESUCCESS);
        }
#else
        m3ApiReturn(__WASI_ENOSYS);
#endif
        if (retlen < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            m3ApiReturn(errno_to_wasi(errno));
        } else if (retlen == buflen) {
            m3ApiReturn(__WASI_ESUCCESS);
        } else {
            buf    += retlen;
            buflen -= retlen;
        }
    }
}

m3ApiRawFunction(m3_wasi_unstable_clock_res_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_clockid_t     , wasi_clk_id)
    m3ApiGetArgMem   (__wasi_timestamp_t*  , resolution)

    if (runtime == NULL || resolution == NULL) { m3ApiReturn(__WASI_EINVAL); }

    int clk = convert_clockid(wasi_clk_id);
    if (clk < 0) m3ApiReturn(__WASI_EINVAL);

    struct timespec tp;
    if (clock_getres(clk, &tp) != 0)
        *resolution = 1000000;
    else
        *resolution = convert_timespec(&tp);

    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_clock_time_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_clockid_t     , wasi_clk_id)
    m3ApiGetArg      (__wasi_timestamp_t   , precision)
    m3ApiGetArgMem   (__wasi_timestamp_t*  , time)

    if (runtime == NULL || time == NULL) { m3ApiReturn(__WASI_EINVAL); }

    int clk = convert_clockid(wasi_clk_id);
    if (clk < 0) m3ApiReturn(__WASI_EINVAL);

    struct timespec tp;
    if (clock_gettime(clk, &tp) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }

    *time = convert_timespec(&tp);
    m3ApiReturn(__WASI_ESUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_proc_exit)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (uint32_t, code)

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


M3Result  m3_LinkEspWASI  (IM3Module module)
{
    M3Result result = m3Err_none;

    const char* wasi  = "wasi_unstable";

    // TODO: Preopen dirs

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "args_sizes_get",       "i(**)",   &m3_wasi_unstable_args_sizes_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_sizes_get",    "i(**)",   &m3_wasi_unstable_environ_sizes_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "args_get",             "i(**)",   &m3_wasi_unstable_args_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_get",          "i(**)",   &m3_wasi_unstable_environ_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_dir_name",  "i(i*i)",  &m3_wasi_unstable_fd_prestat_dir_name)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_get",       "i(i*)",   &m3_wasi_unstable_fd_prestat_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_open",            "i(ii*iiiii*)",  &m3_wasi_unstable_path_open)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_fdstat_get",        "i(i*)",   &m3_wasi_unstable_fd_fdstat_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_write",             "i(iii*)", &m3_wasi_unstable_fd_write)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_read",              "i(iii*)", &m3_wasi_unstable_fd_read)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_seek",              "i(iii*)", &m3_wasi_unstable_fd_seek)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_datasync",          "i(i)",    &m3_wasi_unstable_fd_datasync)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_close",             "i(i)",    &m3_wasi_unstable_fd_close)));

//_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sock_send",            "i()",  &...)));
//_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sock_recv",            "i()",  &...)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "random_get",           "i(*i)",   &m3_wasi_unstable_random_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "clock_res_get",        "i(i*)",   &m3_wasi_unstable_clock_res_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "clock_time_get",       "i(ii*)",  &m3_wasi_unstable_clock_time_get)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "proc_exit",            "i(i)",    &m3_wasi_unstable_proc_exit)));

_catch:
    return result;
}

#endif // ESP32

