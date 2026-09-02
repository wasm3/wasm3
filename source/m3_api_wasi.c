//
//  m3_api_wasi.c
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#include "m3_api_wasi.h"

#include "m3_env.h"
#include "m3_exception.h"

#if defined(d_m3HasWASI)

// Fixup wasi_core.h
#  if defined(M3_COMPILER_MSVC)
#    define _Static_assert(...)
#    define __attribute__(...)
#    define _Noreturn
#  endif

#  include "extra/wasi_core.h"

#  include <sys/types.h>
#  include <sys/stat.h>
#  include <time.h>
#  include <errno.h>
#  include <stdio.h>
#  include <fcntl.h>

#  if defined(__wasi__) || defined(__APPLE__) || defined(__ANDROID_API__) || defined(__OpenBSD__) || defined(__linux__) || defined(__EMSCRIPTEN__) || defined(__CYGWIN__)
#    include <unistd.h>
#    include <sys/uio.h>
#    if defined(__APPLE__)
#      include <TargetConditionals.h>
#      if TARGET_OS_OSX // TARGET_OS_MAC includes iOS
#        include <sys/random.h>
#      else // iOS / Simulator
#        include <Security/Security.h>
#      endif
#    else
#      include <sys/random.h>
#    endif
#    define HAS_IOVEC
#  elif defined(_WIN32)
#    include <Windows.h>
#    include <io.h>
// See http://msdn.microsoft.com/en-us/library/windows/desktop/aa387694.aspx
#    define SystemFunction036 NTAPI SystemFunction036
#    include <NTSecAPI.h>
#    undef SystemFunction036
#    define ssize_t SSIZE_T

#    define open  _open
#    define read  _read
#    define write _write
#    define close _close
#  endif

static m3_wasi_context_t* wasi_context;

typedef struct wasi_iovec_t {
    __wasi_size_t buf;
    __wasi_size_t buf_len;
} wasi_iovec_t;

#  define PREOPEN_CNT   5

// The guest's filestat, in each revision's layout.
#  define d_m3WasiUnstableFilestatSize  56
#  define d_m3WasiFilestatSize          64

// Guest paths arrive as (pointer, length); the host calls want them NUL-terminated,
// so they are copied out into a buffer of this size.
#  define d_m3WasiMaxPath               512

typedef struct Preopen {
    int         fd;
    const char* path;
    const char* real_path;
} Preopen;

static const char* DEFAULT_ENVIRONMENT[] = {
    d_m3WasiDefaultEnvironment,
    NULL,
};

Preopen preopen[PREOPEN_CNT] = {
    { 0,  "<stdin>",  ""  },
    { 1,  "<stdout>", ""  },
    { 2,  "<stderr>", ""  },
    { -1, "/",        "." },
    { -1, "./",       "." },
};

static
__wasi_errno_t errno_to_wasi (int errnum)
{
    // clang-format off
    switch (errnum) {
    case EPERM:    return __WASI_ERRNO_PERM;
    case ENOENT:   return __WASI_ERRNO_NOENT;
    case ESRCH:    return __WASI_ERRNO_SRCH;
    case EINTR:    return __WASI_ERRNO_INTR;
    case EIO:      return __WASI_ERRNO_IO;
    case ENXIO:    return __WASI_ERRNO_NXIO;
    case E2BIG:    return __WASI_ERRNO_2BIG;
    case ENOEXEC:  return __WASI_ERRNO_NOEXEC;
    case EBADF:    return __WASI_ERRNO_BADF;
    case ECHILD:   return __WASI_ERRNO_CHILD;
    case EAGAIN:   return __WASI_ERRNO_AGAIN;
    case ENOMEM:   return __WASI_ERRNO_NOMEM;
    case EACCES:   return __WASI_ERRNO_ACCES;
    case EFAULT:   return __WASI_ERRNO_FAULT;
    case EBUSY:    return __WASI_ERRNO_BUSY;
    case EEXIST:   return __WASI_ERRNO_EXIST;
    case EXDEV:    return __WASI_ERRNO_XDEV;
    case ENODEV:   return __WASI_ERRNO_NODEV;
    case ENOTDIR:  return __WASI_ERRNO_NOTDIR;
    case EISDIR:   return __WASI_ERRNO_ISDIR;
    case EINVAL:   return __WASI_ERRNO_INVAL;
    case ENFILE:   return __WASI_ERRNO_NFILE;
    case EMFILE:   return __WASI_ERRNO_MFILE;
    case ENOTTY:   return __WASI_ERRNO_NOTTY;
    case ETXTBSY:  return __WASI_ERRNO_TXTBSY;
    case EFBIG:    return __WASI_ERRNO_FBIG;
    case ENOSPC:   return __WASI_ERRNO_NOSPC;
    case ESPIPE:   return __WASI_ERRNO_SPIPE;
    case EROFS:    return __WASI_ERRNO_ROFS;
    case EMLINK:   return __WASI_ERRNO_MLINK;
    case EPIPE:    return __WASI_ERRNO_PIPE;
    case EDOM:     return __WASI_ERRNO_DOM;
    case ERANGE:   return __WASI_ERRNO_RANGE;
    }
    // clang-format on
    return __WASI_ERRNO_INVAL;
}

#  if defined(_WIN32)

#    if !defined(__MINGW32__)

static inline
int clock_gettime (int clk_id, struct timespec* spec)
{
    __int64 wintime;
    GetSystemTimeAsFileTime((FILETIME*)&wintime);
    wintime -= 116444736000000000i64;           //1jan1601 to 1jan1970
    spec->tv_sec  = wintime / 10000000i64;           //seconds
    spec->tv_nsec = wintime % 10000000i64 * 100;      //nano-seconds
    return 0;
}

static inline
int clock_getres (int clk_id, struct timespec* spec)
{
    return -1; // Defaults to 1000000
}

#    endif

// the clock_gettime/clock_getres above take a plain int
typedef int m3_clockid_t;
#    define d_m3ClockIdInvalid  ((m3_clockid_t) -1)

static inline
m3_clockid_t convert_clockid (__wasi_clockid_t in)
{
    return 0;
}

#  else // _WIN32

// wasi-libc (>= wasi-sdk 12) makes clockid_t an opaque pointer rather than an int,
// so the "no such clock" sentinel has to be the null one
typedef clockid_t m3_clockid_t;
#    if defined(__wasi__)
#      define d_m3ClockIdInvalid  ((m3_clockid_t) NULL)
#    else
#      define d_m3ClockIdInvalid  ((m3_clockid_t) -1)
#    endif

static inline
m3_clockid_t convert_clockid (__wasi_clockid_t in)
{
    switch (in) {
    case __WASI_CLOCKID_MONOTONIC: return CLOCK_MONOTONIC;
    case __WASI_CLOCKID_REALTIME: return CLOCK_REALTIME;
#    if defined(CLOCK_PROCESS_CPUTIME_ID)
    case __WASI_CLOCKID_PROCESS_CPUTIME_ID: return CLOCK_PROCESS_CPUTIME_ID;
#    endif
#    if defined(CLOCK_THREAD_CPUTIME_ID)
    case __WASI_CLOCKID_THREAD_CPUTIME_ID: return CLOCK_THREAD_CPUTIME_ID;
#    endif
    default: return d_m3ClockIdInvalid;
    }
}

#  endif // _WIN32

static inline
__wasi_timestamp_t convert_timespec (const struct timespec* ts)
{
    if (ts->tv_sec < 0) {
        return 0;
    }
    if ((__wasi_timestamp_t)ts->tv_sec >= UINT64_MAX / 1000000000) {
        return UINT64_MAX;
    }
    return (__wasi_timestamp_t)ts->tv_sec * 1000000000 + ts->tv_nsec;
}

#  if defined(HAS_IOVEC)

static inline
const void* copy_iov_to_host (IM3Runtime runtime, void* _mem, struct iovec* host_iov, wasi_iovec_t* wasi_iov, int32_t iovs_len)
{
    // Convert wasi memory offsets to host addresses
    for (int i = 0; i < iovs_len; i++) {
        host_iov[i].iov_base = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iov[i].buf));
        host_iov[i].iov_len  = m3ApiReadMem32(&wasi_iov[i].buf_len);
        m3ApiCheckMem(host_iov[i].iov_base, host_iov[i].iov_len);
    }
    m3ApiSuccess();
}

#  endif

/*
 * WASI API implementation
 */

m3ApiRawFunction(m3_wasi_generic_args_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint32_t*, argv)
    m3ApiGetArgMem(char*, argv_buf)

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) {
        m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    m3ApiCheckMem(argv, context->argc * sizeof(uint32_t));

    for (u32 i = 0; i < context->argc; ++i) {
        m3ApiWriteMem32(&argv[i], m3ApiPtrToOffset(argv_buf));

        size_t len = strlen(context->argv[i]);

        m3ApiCheckMem(argv_buf, len + 1);       // include the terminating `\0`
        memcpy(argv_buf, context->argv[i], len);
        argv_buf += len;
        *argv_buf++ = 0;
    }

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_args_sizes_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(__wasi_size_t*, argc)
    m3ApiGetArgMem(__wasi_size_t*, argv_buf_size)

    m3ApiCheckMem(argc, sizeof(__wasi_size_t));
    m3ApiCheckMem(argv_buf_size, sizeof(__wasi_size_t));

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) {
        m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    __wasi_size_t buf_len = 0;
    for (u32 i = 0; i < context->argc; ++i) {
        buf_len += (__wasi_size_t)strlen(context->argv[i]) + 1;
    }

    m3ApiWriteMem32(argc, context->argc);
    m3ApiWriteMem32(argv_buf_size, buf_len);

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_environ_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint32_t*, env)
    m3ApiGetArgMem(char*, env_buf)

    __wasi_size_t count = 0;
    for (const char** e = DEFAULT_ENVIRONMENT; *e; ++e) {
        count++;
    }

    m3ApiCheckMem(env, count * sizeof(uint32_t));

    for (u32 i = 0; i < count; ++i) {
        m3ApiWriteMem32(&env[i], m3ApiPtrToOffset(env_buf));

        size_t len = strlen(DEFAULT_ENVIRONMENT[i]);

        m3ApiCheckMem(env_buf, len + 1);        // include the terminating `\0`
        memcpy(env_buf, DEFAULT_ENVIRONMENT[i], len);
        env_buf += len;
        *env_buf++ = 0;
    }

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_environ_sizes_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(__wasi_size_t*, env_count)
    m3ApiGetArgMem(__wasi_size_t*, env_buf_size)

    m3ApiCheckMem(env_count, sizeof(__wasi_size_t));
    m3ApiCheckMem(env_buf_size, sizeof(__wasi_size_t));

    __wasi_size_t count = 0, buf_len = 0;
    for (const char** e = DEFAULT_ENVIRONMENT; *e; ++e) {
        count++;
        buf_len += (__wasi_size_t)strlen(*e) + 1;
    }

    m3ApiWriteMem32(env_count, count);
    m3ApiWriteMem32(env_buf_size, buf_len);

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_prestat_dir_name)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(char*, path)
    m3ApiGetArg(__wasi_size_t, path_len)

    m3ApiCheckMem(path, path_len);

    if (fd < 3 || fd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }
    size_t slen = strlen(preopen[fd].path) + 1;
    memcpy(path, preopen[fd].path, M3_MIN(slen, path_len));
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_prestat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(uint8_t*, buf)

    m3ApiCheckMem(buf, 8);

    if (fd < 3 || fd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    m3ApiWriteMem32(buf + 0, __WASI_PREOPENTYPE_DIR);
    m3ApiWriteMem32(buf + 4, (u32)strlen(preopen[fd].path) + 1);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

#  if !defined(_WIN32)
static
__wasi_filetype_t filetype_from_stat_mode (int mode)
{
    return (S_ISBLK(mode) ? __WASI_FILETYPE_BLOCK_DEVICE : 0) |
           (S_ISCHR(mode) ? __WASI_FILETYPE_CHARACTER_DEVICE : 0) |
           (S_ISDIR(mode) ? __WASI_FILETYPE_DIRECTORY : 0) |
           (S_ISREG(mode) ? __WASI_FILETYPE_REGULAR_FILE : 0) |
           //(S_ISSOCK(mode)  ? __WASI_FILETYPE_SOCKET_STREAM    : 0) |
           (S_ISLNK(mode) ? __WASI_FILETYPE_SYMBOLIC_LINK : 0);
}

#  endif

m3ApiRawFunction(m3_wasi_generic_fd_fdstat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(__wasi_fdstat_t*, fdstat)

    m3ApiCheckMem(fdstat, sizeof(__wasi_fdstat_t));

#  ifdef _WIN32

    // TODO: This needs a proper implementation
    if (fd < PREOPEN_CNT) {
        fdstat->fs_filetype = __WASI_FILETYPE_DIRECTORY;
    } else {
        fdstat->fs_filetype = __WASI_FILETYPE_REGULAR_FILE;
    }

    fdstat->fs_flags             = 0;
    fdstat->fs_rights_base       = (uint64_t)-1; // all rights
    fdstat->fs_rights_inheriting = (uint64_t)-1; // all rights
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  else
    struct stat fd_stat;

    int fl = fcntl(fd, F_GETFL);
    if (fl < 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }

    fstat(fd, &fd_stat);
    fdstat->fs_filetype = filetype_from_stat_mode(fd_stat.st_mode);
    m3ApiWriteMem16(&fdstat->fs_flags,
                    ((fl & O_APPEND) ? __WASI_FDFLAGS_APPEND : 0) |
                      ((fl & O_DSYNC) ? __WASI_FDFLAGS_DSYNC : 0) |
                      ((fl & O_NONBLOCK) ? __WASI_FDFLAGS_NONBLOCK : 0) |
                      //((fl & O_RSYNC)     ? __WASI_FDFLAGS_RSYNC     : 0) |
                      ((fl & O_SYNC) ? __WASI_FDFLAGS_SYNC : 0));

    fdstat->fs_rights_base = (uint64_t)-1; // all rights

    // Make descriptors 0,1,2 look like a TTY
    if (fd <= 2) {
        fdstat->fs_rights_base &= ~(__WASI_RIGHTS_FD_SEEK | __WASI_RIGHTS_FD_TELL);
    }

    fdstat->fs_rights_inheriting = (uint64_t)-1; // all rights
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_fd_fdstat_set_flags)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_fdflags_t, flags)

    // TODO

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_advise)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_filesize_t, offset)
    m3ApiGetArg(__wasi_filesize_t, length)
    m3ApiGetArg(__wasi_advice_t, advice)

    // Purely advisory: passing it on is a best effort, and dropping it is
    // still a conforming implementation. Only the fd has to be valid.
#  if defined(POSIX_FADV_NORMAL)
    int adv;
    switch (advice) {
    case __WASI_ADVICE_NORMAL: adv = POSIX_FADV_NORMAL; break;
    case __WASI_ADVICE_SEQUENTIAL: adv = POSIX_FADV_SEQUENTIAL; break;
    case __WASI_ADVICE_RANDOM: adv = POSIX_FADV_RANDOM; break;
    case __WASI_ADVICE_WILLNEED: adv = POSIX_FADV_WILLNEED; break;
    case __WASI_ADVICE_DONTNEED: adv = POSIX_FADV_DONTNEED; break;
    case __WASI_ADVICE_NOREUSE: adv = POSIX_FADV_NOREUSE; break;
    default: m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    int ret = posix_fadvise(fd, offset, length, adv);
    if (ret != 0) {
        m3ApiReturn(errno_to_wasi(ret));
    }
#  else
    if (advice > __WASI_ADVICE_NOREUSE) {
        m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    struct stat fd_stat;
    if (fstat(fd, &fd_stat) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
#  endif

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_filestat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(uint8_t*, buf)

    m3ApiCheckMem(buf, d_m3WasiUnstableFilestatSize);

#  ifdef _WIN32

    // TODO: This needs a proper implementation, as for fd_fdstat_get
    memset(buf, 0, d_m3WasiUnstableFilestatSize);
    m3ApiWriteMem8(buf + 16, (fd < PREOPEN_CNT) ? __WASI_FILETYPE_DIRECTORY
                                                : __WASI_FILETYPE_REGULAR_FILE);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  else
    struct stat fd_stat;
    if (fstat(fd, &fd_stat) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }

    memset(buf, 0, d_m3WasiUnstableFilestatSize);
    m3ApiWriteMem64(buf + 0, fd_stat.st_dev);
    m3ApiWriteMem64(buf + 8, fd_stat.st_ino);
    m3ApiWriteMem8(buf + 16, filetype_from_stat_mode(fd_stat.st_mode));
    m3ApiWriteMem32(buf + 20, (uint32_t)fd_stat.st_nlink);
    m3ApiWriteMem64(buf + 24, (uint64_t)fd_stat.st_size);
    m3ApiWriteMem64(buf + 32, (uint64_t)fd_stat.st_atime * 1000000000ULL);
    m3ApiWriteMem64(buf + 40, (uint64_t)fd_stat.st_mtime * 1000000000ULL);
    m3ApiWriteMem64(buf + 48, (uint64_t)fd_stat.st_ctime * 1000000000ULL);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_snapshot_preview1_fd_filestat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(uint8_t*, buf)

    m3ApiCheckMem(buf, d_m3WasiFilestatSize);

#  ifdef _WIN32

    // TODO: This needs a proper implementation, as for fd_fdstat_get
    memset(buf, 0, d_m3WasiFilestatSize);
    m3ApiWriteMem8(buf + 16, (fd < PREOPEN_CNT) ? __WASI_FILETYPE_DIRECTORY
                                                : __WASI_FILETYPE_REGULAR_FILE);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  else
    struct stat fd_stat;
    if (fstat(fd, &fd_stat) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }

    memset(buf, 0, d_m3WasiFilestatSize);
    m3ApiWriteMem64(buf + 0, fd_stat.st_dev);
    m3ApiWriteMem64(buf + 8, fd_stat.st_ino);
    m3ApiWriteMem8(buf + 16, filetype_from_stat_mode(fd_stat.st_mode));
    m3ApiWriteMem64(buf + 24, (uint64_t)fd_stat.st_nlink);
    m3ApiWriteMem64(buf + 32, (uint64_t)fd_stat.st_size);
    m3ApiWriteMem64(buf + 40, (uint64_t)fd_stat.st_atime * 1000000000ULL);
    m3ApiWriteMem64(buf + 48, (uint64_t)fd_stat.st_mtime * 1000000000ULL);
    m3ApiWriteMem64(buf + 56, (uint64_t)fd_stat.st_ctime * 1000000000ULL);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

// Copies a path out of guest memory so it can be handed to the host NUL-terminated.
// The caller has already bounds-checked (i_path, i_pathLen) against guest memory.
static
int copy_path (char* o_path, const char* i_path, __wasi_size_t i_pathLen)
{
    if (i_pathLen >= d_m3WasiMaxPath) {
        return 0;
    }
    memcpy(o_path, i_path, i_pathLen);
    o_path[i_pathLen] = '\0';
    return 1;
}

m3ApiRawFunction(m3_wasi_unstable_path_filestat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, dirfd)
    m3ApiGetArg(__wasi_lookupflags_t, dirflags)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(__wasi_size_t, path_len)
    m3ApiGetArgMem(uint8_t*, buf)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(buf, d_m3WasiUnstableFilestatSize);

    char host_path[d_m3WasiMaxPath];
    if (!copy_path(host_path, path, path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

#  if defined(_WIN32)
    // TODO: needs an fstatat equivalent, the same gap path_open has here
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    struct stat path_stat;
    int         flags = (dirflags & __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW) ? 0 : AT_SYMLINK_NOFOLLOW;

    if (fstatat(preopen[dirfd].fd, host_path, &path_stat, flags) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }

    memset(buf, 0, d_m3WasiUnstableFilestatSize);
    m3ApiWriteMem64(buf + 0, path_stat.st_dev);
    m3ApiWriteMem64(buf + 8, path_stat.st_ino);
    m3ApiWriteMem8(buf + 16, filetype_from_stat_mode(path_stat.st_mode));
    m3ApiWriteMem32(buf + 20, (uint32_t)path_stat.st_nlink);
    m3ApiWriteMem64(buf + 24, (uint64_t)path_stat.st_size);
    m3ApiWriteMem64(buf + 32, (uint64_t)path_stat.st_atime * 1000000000ULL);
    m3ApiWriteMem64(buf + 40, (uint64_t)path_stat.st_mtime * 1000000000ULL);
    m3ApiWriteMem64(buf + 48, (uint64_t)path_stat.st_ctime * 1000000000ULL);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_snapshot_preview1_path_filestat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, dirfd)
    m3ApiGetArg(__wasi_lookupflags_t, dirflags)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(__wasi_size_t, path_len)
    m3ApiGetArgMem(uint8_t*, buf)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(buf, d_m3WasiFilestatSize);

    char host_path[d_m3WasiMaxPath];
    if (!copy_path(host_path, path, path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

#  if defined(_WIN32)
    // TODO: needs an fstatat equivalent, the same gap path_open has here
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    struct stat path_stat;
    int         flags = (dirflags & __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW) ? 0 : AT_SYMLINK_NOFOLLOW;

    if (fstatat(preopen[dirfd].fd, host_path, &path_stat, flags) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }

    memset(buf, 0, d_m3WasiFilestatSize);
    m3ApiWriteMem64(buf + 0, path_stat.st_dev);
    m3ApiWriteMem64(buf + 8, path_stat.st_ino);
    m3ApiWriteMem8(buf + 16, filetype_from_stat_mode(path_stat.st_mode));
    m3ApiWriteMem64(buf + 24, (uint64_t)path_stat.st_nlink);
    m3ApiWriteMem64(buf + 32, (uint64_t)path_stat.st_size);
    m3ApiWriteMem64(buf + 40, (uint64_t)path_stat.st_atime * 1000000000ULL);
    m3ApiWriteMem64(buf + 48, (uint64_t)path_stat.st_mtime * 1000000000ULL);
    m3ApiWriteMem64(buf + 56, (uint64_t)path_stat.st_ctime * 1000000000ULL);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_fd_filestat_set_size)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_filesize_t, size)

#  if defined(_WIN32)
    // TODO: needs ftruncate
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (ftruncate(fd, (off_t)size) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_fd_filestat_set_times)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_timestamp_t, atim)
    m3ApiGetArg(__wasi_timestamp_t, mtim)
    m3ApiGetArg(__wasi_fstflags_t, flags)

#  if defined(_WIN32)
    // TODO: needs futimens
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    // a time is either given, asked for as now, or left alone
    struct timespec times[2];

    if (flags & __WASI_FSTFLAGS_ATIM_NOW) {
        times[0].tv_sec  = 0;
        times[0].tv_nsec = UTIME_NOW;
    } else if (flags & __WASI_FSTFLAGS_ATIM) {
        times[0].tv_sec  = atim / 1000000000ULL;
        times[0].tv_nsec = atim % 1000000000ULL;
    } else {
        times[0].tv_sec  = 0;
        times[0].tv_nsec = UTIME_OMIT;
    }

    if (flags & __WASI_FSTFLAGS_MTIM_NOW) {
        times[1].tv_sec  = 0;
        times[1].tv_nsec = UTIME_NOW;
    } else if (flags & __WASI_FSTFLAGS_MTIM) {
        times[1].tv_sec  = mtim / 1000000000ULL;
        times[1].tv_nsec = mtim % 1000000000ULL;
    } else {
        times[1].tv_sec  = 0;
        times[1].tv_nsec = UTIME_OMIT;
    }

    if (futimens(fd, times) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_path_link)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, old_dirfd)
    m3ApiGetArg(__wasi_lookupflags_t, old_flags)
    m3ApiGetArgMem(const char*, old_path)
    m3ApiGetArg(__wasi_size_t, old_path_len)
    m3ApiGetArg(__wasi_fd_t, new_dirfd)
    m3ApiGetArgMem(const char*, new_path)
    m3ApiGetArg(__wasi_size_t, new_path_len)

    m3ApiCheckMem(old_path, old_path_len);
    m3ApiCheckMem(new_path, new_path_len);

    char host_old[d_m3WasiMaxPath];
    char host_new[d_m3WasiMaxPath];
    if (!copy_path(host_old, old_path, old_path_len) ||
        !copy_path(host_new, new_path, new_path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

#  if defined(_WIN32)
    // TODO: needs linkat, the same gap path_open has here
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (old_dirfd >= PREOPEN_CNT || new_dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    int flags = (old_flags & __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW) ? AT_SYMLINK_FOLLOW : 0;

    if (linkat(preopen[old_dirfd].fd, host_old, preopen[new_dirfd].fd, host_new, flags) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_path_create_directory)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, dirfd)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(__wasi_size_t, path_len)

    m3ApiCheckMem(path, path_len);

    char host_path[d_m3WasiMaxPath];
    if (!copy_path(host_path, path, path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

#  if defined(_WIN32)
    // TODO: needs mkdirat, the same gap path_open has here
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    if (mkdirat(preopen[dirfd].fd, host_path, 0755) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_path_remove_directory)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, dirfd)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(__wasi_size_t, path_len)

    m3ApiCheckMem(path, path_len);

    char host_path[d_m3WasiMaxPath];
    if (!copy_path(host_path, path, path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

#  if defined(_WIN32)
    // TODO: needs unlinkat, the same gap path_open has here
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    if (unlinkat(preopen[dirfd].fd, host_path, AT_REMOVEDIR) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_path_unlink_file)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, dirfd)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(__wasi_size_t, path_len)

    m3ApiCheckMem(path, path_len);

    char host_path[d_m3WasiMaxPath];
    if (!copy_path(host_path, path, path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

#  if defined(_WIN32)
    // TODO: needs unlinkat, the same gap path_open has here
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    if (unlinkat(preopen[dirfd].fd, host_path, 0) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_path_rename)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, old_dirfd)
    m3ApiGetArgMem(const char*, old_path)
    m3ApiGetArg(__wasi_size_t, old_path_len)
    m3ApiGetArg(__wasi_fd_t, new_dirfd)
    m3ApiGetArgMem(const char*, new_path)
    m3ApiGetArg(__wasi_size_t, new_path_len)

    m3ApiCheckMem(old_path, old_path_len);
    m3ApiCheckMem(new_path, new_path_len);

    char host_old[d_m3WasiMaxPath];
    char host_new[d_m3WasiMaxPath];
    if (!copy_path(host_old, old_path, old_path_len) ||
        !copy_path(host_new, new_path, new_path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

#  if defined(_WIN32)
    // TODO: needs renameat, the same gap path_open has here
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (old_dirfd >= PREOPEN_CNT || new_dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    if (renameat(preopen[old_dirfd].fd, host_old, preopen[new_dirfd].fd, host_new) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_path_symlink)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(const char*, old_path)
    m3ApiGetArg(__wasi_size_t, old_path_len)
    m3ApiGetArg(__wasi_fd_t, dirfd)
    m3ApiGetArgMem(const char*, new_path)
    m3ApiGetArg(__wasi_size_t, new_path_len)

    m3ApiCheckMem(old_path, old_path_len);
    m3ApiCheckMem(new_path, new_path_len);

    // the target is stored as given, so it is a string rather than a path to resolve
    char host_old[d_m3WasiMaxPath];
    char host_new[d_m3WasiMaxPath];
    if (!copy_path(host_old, old_path, old_path_len) ||
        !copy_path(host_new, new_path, new_path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

#  if defined(_WIN32)
    // TODO: needs symlinkat, the same gap path_open has here
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    if (symlinkat(host_old, preopen[dirfd].fd, host_new) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_path_readlink)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, dirfd)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(__wasi_size_t, path_len)
    m3ApiGetArgMem(char*, buf)
    m3ApiGetArg(__wasi_size_t, buf_len)
    m3ApiGetArgMem(__wasi_size_t*, bufused)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(buf, buf_len);
    m3ApiCheckMem(bufused, sizeof(__wasi_size_t));

    char host_path[d_m3WasiMaxPath];
    if (!copy_path(host_path, path, path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

#  if defined(_WIN32)
    // TODO: needs readlinkat, the same gap path_open has here
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    if (dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    // the link target is written unterminated; bufused is how much of it fit
    ssize_t ret = readlinkat(preopen[dirfd].fd, host_path, buf, buf_len);
    if (ret < 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiWriteMem32(bufused, (uint32_t)ret);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_fd_tell)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(__wasi_filesize_t*, result)

    m3ApiCheckMem(result, sizeof(__wasi_filesize_t));

    int64_t ret;
#  if defined(_WIN32)
    ret = _lseeki64(fd, 0, SEEK_CUR);
#  else
    ret = lseek(fd, 0, SEEK_CUR);
#  endif
    if (ret < 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiWriteMem64(result, ret);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_seek)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_filedelta_t, offset)
    m3ApiGetArg(uint32_t, wasi_whence)
    m3ApiGetArgMem(__wasi_filesize_t*, result)

    m3ApiCheckMem(result, sizeof(__wasi_filesize_t));

    int whence;

    switch (wasi_whence) {
    case 0: whence = SEEK_CUR; break;
    case 1: whence = SEEK_END; break;
    case 2: whence = SEEK_SET; break;
    default: m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    int64_t ret;
#  if defined(_WIN32)
    ret = _lseeki64(fd, offset, whence);
#  else
    ret = lseek(fd, offset, whence);
#  endif
    if (ret < 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiWriteMem64(result, ret);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_snapshot_preview1_fd_seek)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_filedelta_t, offset)
    m3ApiGetArg(uint32_t, wasi_whence)
    m3ApiGetArgMem(__wasi_filesize_t*, result)

    m3ApiCheckMem(result, sizeof(__wasi_filesize_t));

    int whence;

    switch (wasi_whence) {
    case 0: whence = SEEK_SET; break;
    case 1: whence = SEEK_CUR; break;
    case 2: whence = SEEK_END; break;
    default: m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    int64_t ret;
#  if defined(_WIN32)
    ret = _lseeki64(fd, offset, whence);
#  else
    ret = lseek(fd, offset, whence);
#  endif
    if (ret < 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiWriteMem64(result, ret);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}


m3ApiRawFunction(m3_wasi_generic_path_open)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, dirfd)
    m3ApiGetArg(__wasi_lookupflags_t, dirflags)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(__wasi_size_t, path_len)
    m3ApiGetArg(__wasi_oflags_t, oflags)
    m3ApiGetArg(__wasi_rights_t, fs_rights_base)
    m3ApiGetArg(__wasi_rights_t, fs_rights_inheriting)
    m3ApiGetArg(__wasi_fdflags_t, fs_flags)
    m3ApiGetArgMem(__wasi_fd_t*, fd)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(fd, sizeof(__wasi_fd_t));

    if (path_len >= 512) {
        m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    // copy path so we can ensure it is NULL terminated
#  if M3_HAS_VLA
    char host_path[path_len + 1];
#  else
    char host_path[512];
#  endif
    memcpy(host_path, path, path_len);
    host_path[path_len] = '\0'; // NULL terminator

#  if defined(_WIN32)
    // TODO: This all needs a proper implementation

    int flags = ((oflags & __WASI_OFLAGS_CREAT) ? _O_CREAT : 0) |
                ((oflags & __WASI_OFLAGS_EXCL) ? _O_EXCL : 0) |
                ((oflags & __WASI_OFLAGS_TRUNC) ? _O_TRUNC : 0) |
                ((fs_flags & __WASI_FDFLAGS_APPEND) ? _O_APPEND : 0) |
                _O_BINARY;

    if ((fs_rights_base & __WASI_RIGHTS_FD_READ) &&
        (fs_rights_base & __WASI_RIGHTS_FD_WRITE)) {
        flags |= _O_RDWR;
    } else if ((fs_rights_base & __WASI_RIGHTS_FD_WRITE)) {
        flags |= _O_WRONLY;
    } else if ((fs_rights_base & __WASI_RIGHTS_FD_READ)) {
        flags |= _O_RDONLY; // no-op because O_RDONLY is 0
    }
    int mode = 0644;

    int host_fd = open(host_path, flags, mode);

    if (host_fd < 0) {
        m3ApiReturn(errno_to_wasi(errno));
    } else {
        m3ApiWriteMem32(fd, host_fd);
        m3ApiReturn(__WASI_ERRNO_SUCCESS);
    }
#  else
    // translate o_flags and fs_flags into flags and mode
    int flags = ((oflags & __WASI_OFLAGS_CREAT) ? O_CREAT : 0) |
                //((oflags & __WASI_OFLAGS_DIRECTORY)         ? O_DIRECTORY : 0) |
                ((oflags & __WASI_OFLAGS_EXCL) ? O_EXCL : 0) |
                ((oflags & __WASI_OFLAGS_TRUNC) ? O_TRUNC : 0) |
                ((fs_flags & __WASI_FDFLAGS_APPEND) ? O_APPEND : 0) |
                ((fs_flags & __WASI_FDFLAGS_DSYNC) ? O_DSYNC : 0) |
                ((fs_flags & __WASI_FDFLAGS_NONBLOCK) ? O_NONBLOCK : 0) |
                //((fs_flags & __WASI_FDFLAGS_RSYNC)      ? O_RSYNC     : 0) |
                ((fs_flags & __WASI_FDFLAGS_SYNC) ? O_SYNC : 0);
    if ((fs_rights_base & __WASI_RIGHTS_FD_READ) &&
        (fs_rights_base & __WASI_RIGHTS_FD_WRITE)) {
        flags |= O_RDWR;
    } else if ((fs_rights_base & __WASI_RIGHTS_FD_WRITE)) {
        flags |= O_WRONLY;
    } else if ((fs_rights_base & __WASI_RIGHTS_FD_READ)) {
        flags |= O_RDONLY; // no-op because O_RDONLY is 0
    }
    if (dirfd >= PREOPEN_CNT) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    int mode    = 0644;
    int host_fd = openat(preopen[dirfd].fd, host_path, flags, mode);

    if (host_fd < 0) {
        m3ApiReturn(errno_to_wasi(errno));
    } else {
        m3ApiWriteMem32(fd, host_fd);
        m3ApiReturn(__WASI_ERRNO_SUCCESS);
    }
#  endif
}

m3ApiRawFunction(m3_wasi_generic_fd_read)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, wasi_iovs)
    m3ApiGetArg(__wasi_size_t, iovs_len)
    m3ApiGetArgMem(__wasi_size_t*, nread)

    m3ApiCheckMem(wasi_iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nread, sizeof(__wasi_size_t));

#  if defined(HAS_IOVEC)
    struct iovec iovs[iovs_len];
    const void*  mem_check = copy_iov_to_host(runtime, _mem, iovs, wasi_iovs, iovs_len);
    if (mem_check != m3Err_none) {
        return mem_check;
    }

    ssize_t ret = readv(fd, iovs, iovs_len);
    if (ret < 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiWriteMem32(nread, ret);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  else
    ssize_t res = 0;
    for (__wasi_size_t i = 0; i < iovs_len; i++) {
        void*  addr = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iovs[i].buf));
        size_t len  = m3ApiReadMem32(&wasi_iovs[i].buf_len);
        if (len == 0) {
            continue;
        }
        m3ApiCheckMem(addr, len);
        int ret = read(fd, addr, (unsigned)len);
        if (ret < 0) {
            m3ApiReturn(errno_to_wasi(errno));
        }
        res += ret;
        if ((size_t)ret < len) {
            break;
        }
    }
    m3ApiWriteMem32(nread, (__wasi_size_t)res);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

// pread/pwrite over the guest's iovecs. The offset walks with the data, so a short
// transfer stops the loop rather than leaving a hole.
m3ApiRawFunction(m3_wasi_generic_fd_pread)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, wasi_iovs)
    m3ApiGetArg(__wasi_size_t, iovs_len)
    m3ApiGetArg(__wasi_filesize_t, offset)
    m3ApiGetArgMem(__wasi_size_t*, nread)

    m3ApiCheckMem(wasi_iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nread, sizeof(__wasi_size_t));

#  if defined(_WIN32)
    // TODO: needs pread
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    ssize_t res = 0;
    for (__wasi_size_t i = 0; i < iovs_len; i++) {
        void*  addr = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iovs[i].buf));
        size_t len  = m3ApiReadMem32(&wasi_iovs[i].buf_len);
        if (len == 0) {
            continue;
        }
        m3ApiCheckMem(addr, len);
        ssize_t ret = pread(fd, addr, len, offset + res);
        if (ret < 0) {
            m3ApiReturn(errno_to_wasi(errno));
        }
        res += ret;
        if ((size_t)ret < len) {
            break;
        }
    }
    m3ApiWriteMem32(nread, (__wasi_size_t)res);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_fd_pwrite)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, wasi_iovs)
    m3ApiGetArg(__wasi_size_t, iovs_len)
    m3ApiGetArg(__wasi_filesize_t, offset)
    m3ApiGetArgMem(__wasi_size_t*, nwritten)

    m3ApiCheckMem(wasi_iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nwritten, sizeof(__wasi_size_t));

#  if defined(_WIN32)
    // TODO: needs pwrite
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#  else
    ssize_t res = 0;
    for (__wasi_size_t i = 0; i < iovs_len; i++) {
        void*  addr = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iovs[i].buf));
        size_t len  = m3ApiReadMem32(&wasi_iovs[i].buf_len);
        if (len == 0) {
            continue;
        }
        m3ApiCheckMem(addr, len);
        ssize_t ret = pwrite(fd, addr, len, offset + res);
        if (ret < 0) {
            m3ApiReturn(errno_to_wasi(errno));
        }
        res += ret;
        if ((size_t)ret < len) {
            break;
        }
    }
    m3ApiWriteMem32(nwritten, (__wasi_size_t)res);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_fd_write)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, wasi_iovs)
    m3ApiGetArg(__wasi_size_t, iovs_len)
    m3ApiGetArgMem(__wasi_size_t*, nwritten)

    m3ApiCheckMem(wasi_iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nwritten, sizeof(__wasi_size_t));

#  if defined(HAS_IOVEC)
    struct iovec iovs[iovs_len];
    const void*  mem_check = copy_iov_to_host(runtime, _mem, iovs, wasi_iovs, iovs_len);
    if (mem_check != m3Err_none) {
        return mem_check;
    }

    ssize_t ret = writev(fd, iovs, iovs_len);
    if (ret < 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }
    m3ApiWriteMem32(nwritten, ret);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  else
    ssize_t res = 0;
    for (__wasi_size_t i = 0; i < iovs_len; i++) {
        void*  addr = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iovs[i].buf));
        size_t len  = m3ApiReadMem32(&wasi_iovs[i].buf_len);
        if (len == 0) {
            continue;
        }
        m3ApiCheckMem(addr, len);
        int ret = write(fd, addr, (unsigned)len);
        if (ret < 0) {
            m3ApiReturn(errno_to_wasi(errno));
        }
        res += ret;
        if ((size_t)ret < len) {
            break;
        }
    }
    m3ApiWriteMem32(nwritten, (__wasi_size_t)res);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
#  endif
}

m3ApiRawFunction(m3_wasi_generic_fd_close)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)

    int ret = close(fd);
    m3ApiReturn(ret == 0 ? __WASI_ERRNO_SUCCESS : ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_datasync)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)

#  if defined(_WIN32)
    int ret = _commit(fd);
#  elif defined(__APPLE__)
    int ret = fsync(fd);
#  elif defined(__ANDROID_API__) || defined(__OpenBSD__) || defined(__linux__) || defined(__EMSCRIPTEN__)
    int ret = fdatasync(fd);
#  else
    int ret = __WASI_ERRNO_NOSYS;
#  endif
    m3ApiReturn(ret == 0 ? __WASI_ERRNO_SUCCESS : ret);
}

m3ApiRawFunction(m3_wasi_generic_random_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint8_t*, buf)
    m3ApiGetArg(__wasi_size_t, buf_len)

    m3ApiCheckMem(buf, buf_len);

    while (1) {
        ssize_t retlen = 0;

#  if defined(__wasi__) || defined(__APPLE__) || defined(__ANDROID_API__) || defined(__OpenBSD__) || defined(__EMSCRIPTEN__)
        size_t reqlen = M3_MIN(buf_len, 256);
#    if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR)
        retlen = SecRandomCopyBytes(kSecRandomDefault, reqlen, buf) < 0 ? -1 : reqlen;
#    else
        retlen = getentropy(buf, reqlen) < 0 ? -1 : reqlen;
#    endif
#  elif defined(__FreeBSD__) || defined(__linux__)
        retlen = getrandom(buf, buf_len, 0);
#  elif defined(_WIN32)
        if (RtlGenRandom(buf, buf_len) == TRUE) {
            m3ApiReturn(__WASI_ERRNO_SUCCESS);
        }
#  else
        m3ApiReturn(__WASI_ERRNO_NOSYS);
#  endif
        if (retlen < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            m3ApiReturn(errno_to_wasi(errno));
        } else if (retlen == buf_len) {
            m3ApiReturn(__WASI_ERRNO_SUCCESS);
        } else {
            buf += retlen;
            buf_len -= (__wasi_size_t)retlen;
        }
    }
}

m3ApiRawFunction(m3_wasi_generic_clock_res_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_clockid_t, wasi_clk_id)
    m3ApiGetArgMem(__wasi_timestamp_t*, resolution)

    m3ApiCheckMem(resolution, sizeof(__wasi_timestamp_t));

    m3_clockid_t clk = convert_clockid(wasi_clk_id);
    if (clk == d_m3ClockIdInvalid) {
        m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    struct timespec tp;
    if (clock_getres(clk, &tp) != 0) {
        m3ApiWriteMem64(resolution, 1000000);
    } else {
        m3ApiWriteMem64(resolution, convert_timespec(&tp));
    }

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_clock_time_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_clockid_t, wasi_clk_id)
    m3ApiGetArg(__wasi_timestamp_t, precision)
    m3ApiGetArgMem(__wasi_timestamp_t*, time)

    m3ApiCheckMem(time, sizeof(__wasi_timestamp_t));

    m3_clockid_t clk = convert_clockid(wasi_clk_id);
    if (clk == d_m3ClockIdInvalid) {
        m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    struct timespec tp;
    if (clock_gettime(clk, &tp) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }

    m3ApiWriteMem64(time, convert_timespec(&tp));
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_proc_exit)
{
    m3ApiGetArg(uint32_t, code)

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context) {
        context->exit_code = code;
    }

    m3ApiTrap(m3Err_trapWasiExit);
}

m3ApiRawFunction(m3_wasi_generic_sched_yield)
{
    m3ApiReturnType(uint32_t)

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}


static
M3Result SuppressLookupFailure (M3Result i_result)
{
    if (i_result == m3Err_functionLookupFailed) {
        return m3Err_none;
    } else {
        return i_result;
    }
}

m3_wasi_context_t* m3_GetWasiContext ()
{
    return wasi_context;
}

void m3_FreeWasi (m3_wasi_context_t* wasi)
{
    m3_Free(wasi);
}

static inline
M3Result _linkWASI (IM3Module module, m3_wasi_context_t* wasi_context)
{
    M3Result result = m3Err_none;

#  ifdef _WIN32
    _setmode(_fileno(stdin), O_BINARY);
    _setmode(_fileno(stdout), O_BINARY);
    _setmode(_fileno(stderr), O_BINARY);

#  else
    // Preopen dirs
    for (int i = 3; i < PREOPEN_CNT; i++) {
        preopen[i].fd = open(preopen[i].real_path, O_RDONLY);
    }
#  endif

    static const char* namespaces[2] = { "wasi_unstable", "wasi_snapshot_preview1" };

    // clang-format off

    // Some functions are incompatible between WASI versions
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_unstable",          "fd_seek",     "i(iIi*)", &m3_wasi_unstable_fd_seek)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_seek",     "i(iIi*)", &m3_wasi_snapshot_preview1_fd_seek)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_unstable",          "fd_filestat_get",   "i(i*)",     &m3_wasi_unstable_fd_filestat_get)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_filestat_get",   "i(i*)",     &m3_wasi_snapshot_preview1_fd_filestat_get)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_unstable",          "path_filestat_get", "i(ii*i*)",  &m3_wasi_unstable_path_filestat_get)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_snapshot_preview1", "path_filestat_get", "i(ii*i*)",  &m3_wasi_snapshot_preview1_path_filestat_get)));

    for (int i = 0; i < 2; i++) {
        const char* wasi = namespaces[i];

_       (SuppressLookupFailure(m3_LinkRawFunctionEx(module, wasi, "args_get",           "i(**)",   &m3_wasi_generic_args_get, wasi_context)));
_       (SuppressLookupFailure(m3_LinkRawFunctionEx(module, wasi, "args_sizes_get",     "i(**)",   &m3_wasi_generic_args_sizes_get, wasi_context)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "clock_res_get",        "i(i*)",   &m3_wasi_generic_clock_res_get)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "clock_time_get",       "i(iI*)",  &m3_wasi_generic_clock_time_get)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "environ_get",          "i(**)",   &m3_wasi_generic_environ_get)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "environ_sizes_get",    "i(**)",   &m3_wasi_generic_environ_sizes_get)));

_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_advise",            "i(iIIi)", &m3_wasi_generic_fd_advise)));
//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_allocate",          "i(iII)",  )));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_close",             "i(i)",    &m3_wasi_generic_fd_close)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_datasync",          "i(i)",    &m3_wasi_generic_fd_datasync)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_fdstat_get",        "i(i*)",   &m3_wasi_generic_fd_fdstat_get)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_fdstat_set_flags",  "i(ii)",   &m3_wasi_generic_fd_fdstat_set_flags)));
//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_fdstat_set_rights", "i(iII)",  )));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_filestat_set_size", "i(iI)",   &m3_wasi_generic_fd_filestat_set_size)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_filestat_set_times","i(iIIi)", &m3_wasi_generic_fd_filestat_set_times)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_pread",             "i(i*iI*)", &m3_wasi_generic_fd_pread)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_prestat_get",       "i(i*)",   &m3_wasi_generic_fd_prestat_get)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_prestat_dir_name",  "i(i*i)",  &m3_wasi_generic_fd_prestat_dir_name)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_pwrite",            "i(i*iI*)", &m3_wasi_generic_fd_pwrite)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_read",              "i(i*i*)", &m3_wasi_generic_fd_read)));
//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_readdir",           "i(i*iI*)",)));
//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_renumber",          "i(ii)",   )));
//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_sync",              "i(i)",    )));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_tell",              "i(i*)",   &m3_wasi_generic_fd_tell)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_write",             "i(i*i*)", &m3_wasi_generic_fd_write)));

_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_create_directory",    "i(i*i)",       &m3_wasi_generic_path_create_directory)));
//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_filestat_set_times",  "i(ii*iIIi)",   )));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_link",                "i(ii*ii*i)",   &m3_wasi_generic_path_link)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_open",                "i(ii*iiIIi*)", &m3_wasi_generic_path_open)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_readlink",            "i(i*i*i*)",    &m3_wasi_generic_path_readlink)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_remove_directory",    "i(i*i)",       &m3_wasi_generic_path_remove_directory)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_rename",              "i(i*ii*i)",    &m3_wasi_generic_path_rename)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_symlink",             "i(*ii*i)",     &m3_wasi_generic_path_symlink)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_unlink_file",         "i(i*i)",       &m3_wasi_generic_path_unlink_file)));

//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "poll_oneoff",          "i(**i*)", &m3_wasi_generic_poll_oneoff)));
_       (SuppressLookupFailure(m3_LinkRawFunctionEx(module, wasi, "proc_exit",          "v(i)",    &m3_wasi_generic_proc_exit, wasi_context)));
//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "proc_raise",           "i(i)",    )));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "random_get",           "i(*i)",   &m3_wasi_generic_random_get)));
_       (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "sched_yield",          "i()",     &m3_wasi_generic_sched_yield)));

//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "sock_recv",            "i(i*ii**)",        )));
//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "sock_send",            "i(i*ii*)",         )));
//_     (SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "sock_shutdown",        "i(ii)",            )));
    }

    // clang-format on

    // WASI addresses the memory the module exports as "memory", not whichever
    // one the calling code happens to be running against. Resolve it once, here,
    // rather than per call. A module that linked none of the above imports has
    // no WASI to satisfy, so it is not asked for the export - link_all offers
    // WASI to every module, and most have neither the imports nor a memory.
    if (Module_HasLinkedHostImport(module, "wasi_unstable") or
        Module_HasLinkedHostImport(module, "wasi_snapshot_preview1")) {
        u32 memoryIndex;

        if (m3_FindExportedMemory(module, "memory", &memoryIndex)) {
            _throw("WASI requires the module to export its memory as \"memory\"");
        }

_       (m3_BindImportMemory(module, "wasi_unstable", memoryIndex));
_       (m3_BindImportMemory(module, "wasi_snapshot_preview1", memoryIndex));
    }

_catch:
    return result;
}

M3Result m3_NewCustomWASI (M3WASI* wasi_p)
{
    *wasi_p = m3_AllocStruct(m3_wasi_context_t);

    return m3Err_none;
}

void m3_FreeCustomWASI (M3WASI wasi)
{
    m3_Free(wasi);
}

M3Result m3_LinkCustomWASI (IM3Module module, M3WASI wasi)
{
    return _linkWASI(module, wasi);
}

M3Result m3_LinkWASI (IM3Module module)
{
    if (!wasi_context) {
        wasi_context = m3_AllocStruct(m3_wasi_context_t);
    }

    return _linkWASI(module, wasi_context);
}

#endif // d_m3HasWASI
