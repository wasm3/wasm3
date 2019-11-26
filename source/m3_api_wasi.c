//
//  m3_api_wasi.c
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#include "m3_host.h"
#include "m3_core.h"
#include "m3_env.h"
#include "m3_module.h"
#include "m3_exception.h"

#define __wasi__

#include "extra/wasi_core.h"


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

typedef uint32_t wasi_size_t;

struct wasi_iovec
{
    wasi_size_t iov_base;
    wasi_size_t iov_len;
};

typedef struct Preopen {
    char       *path;
    unsigned   path_len;
} Preopen;

#define PREOPEN_CNT 7

Preopen      preopen[PREOPEN_CNT] = {
    { .path = "<stdin>",  .path_len = 7, },
    { .path = "<stdout>", .path_len = 8, },
    { .path = "<stderr>", .path_len = 8, },
    { .path = "./",       .path_len = 2, },
    { .path = "../",      .path_len = 3, },
    { .path = "/",        .path_len = 1, },
    { .path = "/tmp",     .path_len = 4, },
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
uint32_t addr2offset(IM3Module m, void *addr) {
    return (u8*)addr - m->memory.wasmPages;
}

static inline
void *offset2addr(IM3Module m, uint32_t offset) {
    return m->memory.wasmPages + offset;
}

static
void copy_iov_to_host(struct iovec* host_iov, IM3Module m, uint32_t iov_offset, int32_t iovs_len)
{
    // Convert wasi_memory offsets to host addresses
    struct wasi_iovec *wasi_iov = offset2addr(m, iov_offset);
    for (int i = 0; i < iovs_len; i++) {
        host_iov[i].iov_base = offset2addr(m, wasi_iov[i].iov_base);
        host_iov[i].iov_len = wasi_iov[i].iov_len;
    }
}


/*
 * WASI API implementation
 */

uint32_t m3_wasi_unstable_args_get(IM3Module    module,
                                   uint32_t argv_offset,
                                   uint32_t argv_buf_offset)
{
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_args_sizes_get(IM3Module    module,
                                         uint32_t argc_offset,
                                         uint32_t argv_buf_size_offset)
{
    wasi_size_t *argc          = offset2addr(module, argc_offset);
    wasi_size_t *argv_buf_size = offset2addr(module, argv_buf_size_offset);

    *argc = 0;
    *argv_buf_size = 0;
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_environ_get(IM3Module    module,
                                      uint32_t environ_ptrs_offset,
                                      uint32_t environ_strs_offset)
{
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_environ_sizes_get(IM3Module    module,
                                            uint32_t environ_count_offset,
                                            uint32_t environ_buf_size_offset)
{
    wasi_size_t *environ_count    = offset2addr(module, environ_count_offset);
    wasi_size_t *environ_buf_size = offset2addr(module, environ_buf_size_offset);
    *environ_count = 0;
    *environ_buf_size = 0;
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_prestat_dir_name(IM3Module    module,
                                              uint32_t fd,
                                              uint32_t path_offset,
                                              uint32_t path_len)
{
    if (fd < 3 || fd >= PREOPEN_CNT) { return __WASI_EBADF; }
    memmove((char *)offset2addr(module, path_offset), preopen[fd].path,
            min(preopen[fd].path_len, path_len));
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_prestat_get(IM3Module    module,
                                         uint32_t fd,
                                         uint32_t buf_offset)
{
    if (fd < 3 || fd >= PREOPEN_CNT) { return __WASI_EBADF; }
    *(uint32_t *)offset2addr(module, buf_offset) = __WASI_PREOPENTYPE_DIR;
    *(uint32_t *)offset2addr(module, buf_offset+4) = preopen[fd].path_len;
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_fdstat_get(IM3Module    module,
                                        __wasi_fd_t fd,
                                        uint32_t fdstat_offset)
{
    struct stat fd_stat;
    __wasi_fdstat_t *fdstat = offset2addr(module, fdstat_offset);
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

uint32_t m3_wasi_unstable_fd_seek(IM3Module    module,
                                  __wasi_fd_t         fd,
                                  __wasi_filedelta_t  offset,
                                  __wasi_whence_t     whence,
                                  uint32_t            newoffset_offset)
{
    __wasi_filesize_t *result = offset2addr(module, newoffset_offset);

    int wasi_whence = whence == __WASI_WHENCE_END ? SEEK_END :
                                __WASI_WHENCE_CUR ? SEEK_CUR : 0;
    int64_t ret = lseek(fd, offset, wasi_whence);
    if (ret < 0) { return errno_to_wasi(errno); }
    *result = ret;
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_read(IM3Module    module,
                                  __wasi_fd_t  fd,
                                  uint32_t     iovs_offset,
                                  size_t       iovs_len,
                                  uint32_t     nread_offset)
{
    struct iovec iovs[iovs_len];
    copy_iov_to_host(iovs, module, iovs_offset, iovs_len);
    size_t *nread      = offset2addr(module, nread_offset);

    ssize_t ret = readv(fd, iovs, iovs_len);
    if (ret < 0) { return errno_to_wasi(errno); }
    *nread = ret;
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_fd_write(IM3Module    module,
                                   __wasi_fd_t  fd,
                                   uint32_t     iovs_offset,
                                   size_t       iovs_len,
                                   uint32_t     nwritten_offset)
{
    struct iovec iovs[iovs_len];
    copy_iov_to_host(iovs, module, iovs_offset, iovs_len);
    size_t *nwritten   = offset2addr(module, nwritten_offset);

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

uint32_t m3_wasi_unstable_clock_time_get(IM3Module    module,
                                         uint32_t clock_id,
                                         uint64_t precision,
                                         uint32_t time_offset)
{
    uint64_t *time = offset2addr(module, time_offset);
    struct timespec tp;
    clock_gettime(clock_id, &tp);
    *time = (uint64_t)tp.tv_sec * 1000000000 + tp.tv_nsec;
    return __WASI_ESUCCESS;
}

uint32_t m3_wasi_unstable_proc_exit(uint32_t code)
{
    exit(code);
}


static
M3Result  SuppressLookupFailure (M3Result i_result)
{
    if (i_result == c_m3Err_functionLookupFailed)
        return c_m3Err_none;
    else
        return i_result;
}


M3Result  m3_LinkWASI  (IM3Module module)
{
    M3Result result = c_m3Err_none;

_   (SuppressLookupFailure (m3_LinkFunction (module, "args_sizes_get",    "i(Mii)",   &m3_wasi_unstable_args_sizes_get)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "environ_sizes_get", "i(Mii)",   &m3_wasi_unstable_environ_sizes_get)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "args_get",          "i(Mii)",   &m3_wasi_unstable_args_get)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "environ_get",       "i(Mii)",   &m3_wasi_unstable_environ_get)));

_   (SuppressLookupFailure (m3_LinkFunction (module, "fd_prestat_dir_name",  "i(Miii)",  &m3_wasi_unstable_fd_prestat_dir_name)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "fd_prestat_get",       "i(Mii)",   &m3_wasi_unstable_fd_prestat_get)));

_   (SuppressLookupFailure (m3_LinkFunction (module, "fd_fdstat_get",  "i(Mii)",   &m3_wasi_unstable_fd_fdstat_get)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "fd_write",       "i(Miiii)", &m3_wasi_unstable_fd_write)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "fd_read",        "i(Miiii)", &m3_wasi_unstable_fd_read)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "fd_seek",        "i(Miiii)", &m3_wasi_unstable_fd_seek)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "fd_datasync",    "i(i)",     &m3_wasi_unstable_fd_datasync)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "fd_close",       "i(i)",     &m3_wasi_unstable_fd_close)));

_   (SuppressLookupFailure (m3_LinkFunction (module, "clock_time_get", "v(MiIi)",  &m3_wasi_unstable_clock_time_get)));
_   (SuppressLookupFailure (m3_LinkFunction (module, "proc_exit",      "v(i)",     &m3_wasi_unstable_proc_exit)));

    _catch: return result;
}


