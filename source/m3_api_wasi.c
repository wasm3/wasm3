//
//  m3_api_wasi.c
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#include "m3_api_wasi.h"

#include "m3_api_defs.h"
#include "m3_env.h"
#include "m3_module.h"
#include "m3_exception.h"

#if defined(d_m3HasWASI)

#include "extra/wasi_core.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/random.h>

#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>

# if defined(__APPLE__) || defined(__ANDROID_API__) || defined(__OpenBSD__)
#   include <unistd.h>
# elif defined(_WIN32)
/* See http://msdn.microsoft.com/en-us/library/windows/desktop/aa387694.aspx */
#   define SystemFunction036 NTAPI SystemFunction036
#   include <NTSecAPI.h>
#   undef SystemFunction036
# endif

//TODO
#define PREOPEN_CNT   5
#define NANOS_PER_SEC 1000000000

typedef uint32_t __wasi_size_t;

struct wasi_iovec
{
    __wasi_size_t iov_base;
    __wasi_size_t iov_len;
};

typedef struct Preopen {
    int         fd;
    char*       path;
} Preopen;

Preopen preopen[PREOPEN_CNT] = {
    {  0, "<stdin>"  },
    {  1, "<stdout>" },
    {  2, "<stderr>" },
    { -1, "./"       },
    { -1, "../"      },
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
    default:      return errno;
    }
}

static inline
uint32_t addr2offset(IM3Runtime m, void *addr) {
    return (u8*)addr - m->memory.wasmPages;
}

static inline
void *offset2addr(IM3Runtime m, uint32_t offset) {
    return m->memory.wasmPages + offset;
}

static
void copy_iov_to_host(struct iovec* host_iov, IM3Runtime runtime, uint32_t iov_offset, int32_t iovs_len)
{
    // Convert wasi_memory offsets to host addresses
    struct wasi_iovec *wasi_iov = offset2addr(runtime, iov_offset);
    for (int i = 0; i < iovs_len; i++) {
        host_iov[i].iov_base = offset2addr(runtime, wasi_iov[i].iov_base);
        host_iov[i].iov_len  = wasi_iov[i].iov_len;
    }
}


/*
 * WASI API implementation
 */


uint32_t m3_wasi_unstable_args_get (IM3Runtime runtime,
                                    u32 *       argv_offset,
                                    u8 *        argv_buf_offset)
{
    if (runtime)
    {
        for (u32 i = 0; i < runtime->argc; ++i)
        {
            argv_offset [i] = addr2offset (runtime, argv_buf_offset);
            
            size_t len = strlen (runtime->argv [i]);
            memcpy (argv_buf_offset, runtime->argv [i], len);
            argv_buf_offset += len;
            * argv_buf_offset++ = 0;
        }
        
        return __WASI_ESUCCESS;
    }
    else return __WASI_EINVAL;
}

uint32_t m3_wasi_unstable_args_sizes_get (IM3Runtime        runtime,
                                          __wasi_size_t *   argc,
                                          __wasi_size_t *   argv_buf_size)
{
    if (runtime == NULL) { return __WASI_EINVAL; }

    *argc = runtime->argc;
    *argv_buf_size = 0;
    for (int i = 0; i < runtime->argc; ++i)
    {
        * argv_buf_size += strlen (runtime->argv [i]) + 1;
    }
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_environ_get(IM3Runtime runtime,
                                      uint32_t   environ_ptrs_offset,
                                      uint32_t   environ_strs_offset)
{
    if (runtime == NULL) { return __WASI_EINVAL; }
    // TODO
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_environ_sizes_get(IM3Runtime runtime,
                                            uint32_t   environ_count_offset,
                                            uint32_t   environ_buf_size_offset)
{
    if (runtime == NULL) { return __WASI_EINVAL; }
    __wasi_size_t *environ_count    = offset2addr(runtime, environ_count_offset);
    __wasi_size_t *environ_buf_size = offset2addr(runtime, environ_buf_size_offset);
    *environ_count = 0; // TODO
    *environ_buf_size = 0; // TODO
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_prestat_dir_name(IM3Runtime runtime,
                                              uint32_t   fd,
                                              char*      path,
                                              uint32_t   path_len)
{
    if (runtime == NULL) { return __WASI_EINVAL; }
    if (fd < 3 || fd >= PREOPEN_CNT) { return __WASI_EBADF; }
    int size = min(strlen(preopen[fd].path), path_len);
    memcpy(path, preopen[fd].path, size);
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_prestat_get(IM3Runtime runtime,
                                         uint32_t   fd,
                                         uint32_t*  buf)
{
    if (runtime == NULL) { return __WASI_EINVAL; }
    if (fd < 3 || fd >= PREOPEN_CNT) { return __WASI_EBADF; }
    *(buf)   = __WASI_PREOPENTYPE_DIR;
    *(buf+1) = strlen(preopen[fd].path);
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_fdstat_get(IM3Runtime       runtime,
                                        __wasi_fd_t      fd,
                                        __wasi_fdstat_t* fdstat)
{
    if (runtime == NULL || fdstat == NULL) { return __WASI_EINVAL; }

    struct stat fd_stat;
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0) { return errno_to_wasi(errno); }
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
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_seek(IM3Runtime          runtime,
                                  __wasi_fd_t         fd,
                                  __wasi_filedelta_t  offset,
                                  __wasi_whence_t     whence,
                                  __wasi_filesize_t*  result)
{
    if (runtime == NULL || result == NULL) { return __WASI_EINVAL; }

    int wasi_whence = whence == __WASI_WHENCE_END ? SEEK_END :
                                __WASI_WHENCE_CUR ? SEEK_CUR : 0;
    int64_t ret;
#if defined(M3_COMPILER_MSVC)
    ret = _lseeki64(fd, offset, wasi_whence);
#else
    ret = lseek(fd, offset, wasi_whence);
#endif
    if (ret < 0) { return errno_to_wasi(errno); }
    *result = ret;
    return __WASI_ESUCCESS;
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
    {
        m3ApiReturn(__WASI_EINVAL);
    }

    // copy path so we can ensure it is NULL terminated
    char host_path [path_len+1];

    memcpy (host_path, path, path_len);
    host_path [path_len] = '\0'; // NULL terminator

    // translate o_flags and fs_flags into flags and mode
    int flags = ((oflags & __WASI_O_CREAT)             ? O_CREAT     : 0) |
                //((oflags & __WASI_O_DIRECTORY)         ? O_DIRECTORY : 0) |
                ((oflags & __WASI_O_EXCL)              ? O_EXCL      : 0) |
                ((oflags & __WASI_O_TRUNC)             ? O_TRUNC     : 0) |
                ((fs_flags & __WASI_FDFLAG_APPEND)     ? O_APPEND    : 0) |
                //((fs_flags & __WASI_FDFLAG_DSYNC)      ? O_DSYNC     : 0) |
                ((fs_flags & __WASI_FDFLAG_NONBLOCK)   ? O_NONBLOCK  : 0) |
                //((fs_flags & __WASI_FDFLAG_RSYNC)      ? O_RSYNC     : 0) |
                ((fs_flags & __WASI_FDFLAG_SYNC)       ? O_SYNC      : 0);
    if ((fs_rights_base & __WASI_RIGHT_FD_READ) &&
        (fs_rights_base & __WASI_RIGHT_FD_WRITE)) {
        flags |= O_RDWR;
    } else if ((fs_rights_base & __WASI_RIGHT_FD_WRITE)) {
        flags |= O_WRONLY;
    } else if ((fs_rights_base & __WASI_RIGHT_FD_READ)) {
        flags |= O_RDONLY; // no-op because O_RDONLY is 0
    }
    int mode = 0644;
    int host_fd = openat (dirfd, host_path, flags, mode);

    if (host_fd < 0)
    {
        m3ApiReturn(errno_to_wasi (errno));
    }
    else
    {
        * fd = host_fd;
        m3ApiReturn(__WASI_ESUCCESS);
    }

}

m3ApiRawFunction(m3_wasi_unstable_fd_read)
{
    m3ApiReturnType  (uint32_t)

    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (uint32_t             , iovs_offset)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (__wasi_size_t*       , nread)

    if (runtime == NULL || nread == NULL) { m3ApiReturn(__WASI_EINVAL); }

    struct iovec iovs[iovs_len];
    copy_iov_to_host(iovs, runtime, iovs_offset, iovs_len);

    ssize_t ret = readv(fd, iovs, iovs_len);
    if (ret < 0) { m3ApiReturn(errno_to_wasi(errno)); }
    *nread = ret;
    m3ApiReturn(__WASI_ESUCCESS);
}

uint32_t m3_wasi_unstable_fd_write(IM3Runtime    runtime,
                                   __wasi_fd_t   fd,
                                   uint32_t      iovs_offset,
                                   __wasi_size_t iovs_len,
                                   __wasi_size_t* nwritten)
{
    if (runtime == NULL || nwritten == NULL) { return __WASI_EINVAL; }

    struct iovec iovs[iovs_len];
    copy_iov_to_host(iovs, runtime, iovs_offset, iovs_len);

    ssize_t ret = writev(fd, iovs, iovs_len);
    if (ret < 0) { return errno_to_wasi(errno); }
    *nwritten = ret;
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_close(uint32_t fd)
{
    int ret = close(fd);
    return ret == 0 ? __WASI_ESUCCESS : ret;
}

uint32_t m3_wasi_unstable_fd_datasync(uint32_t fd)
{
    int ret = fdatasync(fd);
    return ret == 0 ? __WASI_ESUCCESS : ret;
}

uint32_t m3_wasi_unstable_random_get(void* buf, __wasi_size_t buflen)
{
    while (1) {
        ssize_t retlen = 0;

#if defined(__wasi__)
        retlen = getentropy(buf, buflen);
        if (retlen == 0) {
            retlen = buflen;
        }
#elif defined(__APPLE__) || defined(__ANDROID_API__) || defined(__OpenBSD__)
        size_t pos = 0;
        for (; pos + 256 < buflen; pos += 256) {
            if (getentropy((char *)buf + pos, 256)) {
                return errno_to_wasi(errno);
            }
        }
        if (getentropy((char *)buf + pos, buflen - pos)) {
            return errno_to_wasi(errno);
        }
        return __WASI_ESUCCESS;

#elif defined(__NetBSD__)
        // TODO
        // sysctl(buf, buflen)
#elif defined(__FreeBSD__) || defined(__linux__)
        retlen = getrandom(buf, buflen, 0);
#elif defined(_WIN32)
        if (RtlGenRandom(buf, buflen) == TRUE) {
            return __WASI_ESUCCESS;
        }
#else
        // use syscall ?
        abort (); // unsupport
        retlen = -1;
#endif
        if (retlen < 0) {
            if (errno == EINTR) { continue; }
            return errno_to_wasi(errno);
        }
        if (retlen == buflen) { return __WASI_ESUCCESS; }
        buf = (void *)((uint8_t *)buf + retlen);
        buflen -= retlen;
    }
}

uint32_t m3_wasi_unstable_clock_res_get(IM3Runtime          runtime,
                                        __wasi_clockid_t    clock_id,
                                        __wasi_timestamp_t* resolution)
{
    if (runtime == NULL || resolution == NULL) { return __WASI_EINVAL; }

    struct timespec tp;
    if (clock_getres(clock_id, &tp) != 0)
        *resolution = 1000000;
    else
        *resolution = (tp.tv_sec * NANOS_PER_SEC) + tp.tv_nsec;
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_clock_time_get(IM3Runtime          runtime,
                                         __wasi_clockid_t    clock_id,
                                         __wasi_timestamp_t  precision,
                                         __wasi_timestamp_t* time)
{
    if (runtime == NULL || time == NULL) { return __WASI_EINVAL; }

    struct timespec tp;
    if (clock_gettime(clock_id, &tp) != 0) { return errno_to_wasi(errno); }

    //printf("=== time: %lu.%09u\n", tp.tv_sec, tp.tv_nsec);
    *time = (uint64_t)tp.tv_sec * NANOS_PER_SEC + tp.tv_nsec;
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_proc_exit(uint32_t code)
{
    exit(code);
}


static
M3Result SuppressLookupFailure(M3Result i_result)
{
    if (i_result == c_m3Err_functionLookupFailed)
        return c_m3Err_none;
    else
        return i_result;
}


M3Result  m3_LinkWASI  (IM3Module module)
{
    M3Result result = c_m3Err_none;

    const char* namespace  = "wasi_unstable";

    // Preopen dirs
    for (int i = 3; i < PREOPEN_CNT; i++) {
        preopen[i].fd = open(preopen[i].path, O_RDONLY);
    }
    
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "args_sizes_get",      "i(R**)",       &m3_wasi_unstable_args_sizes_get)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "environ_sizes_get",   "i(Rii)",       &m3_wasi_unstable_environ_sizes_get)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "args_get",            "i(R**)",       &m3_wasi_unstable_args_get)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "environ_get",         "i(Rii)",       &m3_wasi_unstable_environ_get)));

_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "fd_prestat_dir_name",  "i(Ri*i)",     &m3_wasi_unstable_fd_prestat_dir_name)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "fd_prestat_get",       "i(Ri*)",      &m3_wasi_unstable_fd_prestat_get)));

_   (SuppressLookupFailure (m3_LinkRawFunction (module, namespace, "path_open",         &m3_wasi_unstable_path_open)));

_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "fd_fdstat_get",       "i(Ri*)",       &m3_wasi_unstable_fd_fdstat_get)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "fd_write",            "i(Riii*)",     &m3_wasi_unstable_fd_write)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, namespace, "fd_read",           &m3_wasi_unstable_fd_read)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "fd_seek",             "i(Riii*)",     &m3_wasi_unstable_fd_seek)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "fd_datasync",         "i(i)",         &m3_wasi_unstable_fd_datasync)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "fd_close",            "i(i)",         &m3_wasi_unstable_fd_close)));

//_   (SuppressLookupFailure (m3_LinkFunction (module, namespace, "sock_send",     "i(Riii*)",    &...)));
//_   (SuppressLookupFailure (m3_LinkFunction (module, namespace, "sock_recv",     "i(Riii*)",    &...)));

_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "random_get",          "v(*i)",        &m3_wasi_unstable_random_get)));

_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "clock_res_get",       "v(Ri*)",       &m3_wasi_unstable_clock_res_get)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "clock_time_get",      "v(RiI*)",      &m3_wasi_unstable_clock_time_get)));
_   (SuppressLookupFailure (m3_LinkCFunction (module, namespace, "proc_exit",           "v(i)",         &m3_wasi_unstable_proc_exit)));

_catch:
    return result;
}

#else  // d_m3HasWASI

M3Result  m3_LinkWASI  (IM3Module module)
{
    return c_m3Err_none;
}

#endif // d_m3HasWASI

