//
//  m3_api_wasi.c
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#include "m3_api_wasi.h"

#include "m3_api_wasi_host.h"
#include "m3_env.h"
#include "m3_exception.h"

#if defined(d_m3HasWASI)

#  include <string.h>

// The system underneath, of which exactly one is ever built. They are headers rather
// than translation units of their own so that neither has to be added to a source
// list - and most of those are build scripts outside this repository.
#  if defined(_WIN32)
#    include "m3_api_wasi_win32.h"
#  else
#    include "m3_api_wasi_posix.h"
#  endif

// This file is the WASI interface and nothing below it: it decodes the guest's
// arguments, bounds-checks every pointer against guest memory, and writes results
// back in the layout the calling revision asks for. The system underneath is reached
// through m3_api_wasi_host.h, which m3_api_wasi_posix.h and m3_api_wasi_win32.h
// implement independently of each other.

static m3_wasi_context_t* wasi_context;

// A scatter/gather list as the guest lays one out
typedef struct wasi_iovec_t {
    __wasi_size_t buf;
    __wasi_size_t buf_len;
} wasi_iovec_t;

// The guest's filestat, in each revision's layout.
#  define d_m3WasiUnstableFilestatSize  56
#  define d_m3WasiFilestatSize          64

static const char* DEFAULT_ENVIRONMENT[] = {
    d_m3WasiDefaultEnvironment,
    NULL,
};

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

// Writes a filestat out in the layout the caller's revision asks for. The two differ
// in the width of nlink, which moves everything after it along by 8. The caller has
// checked the buffer for the size its own revision names.
static
void write_filestat (uint8_t* o_buf, const m3_wasi_filestat_t* i_stat, bool i_unstable)
{
    memset(o_buf, 0, i_unstable ? d_m3WasiUnstableFilestatSize : d_m3WasiFilestatSize);

    m3ApiWriteMem64(o_buf + 0, i_stat->dev);
    m3ApiWriteMem64(o_buf + 8, i_stat->ino);
    m3ApiWriteMem8(o_buf + 16, i_stat->filetype);

    if (i_unstable) {
        m3ApiWriteMem32(o_buf + 20, (uint32_t)i_stat->nlink);
        o_buf += 24;
    } else {
        m3ApiWriteMem64(o_buf + 24, i_stat->nlink);
        o_buf += 32;
    }

    m3ApiWriteMem64(o_buf + 0, i_stat->size);
    m3ApiWriteMem64(o_buf + 8, i_stat->atim);
    m3ApiWriteMem64(o_buf + 16, i_stat->mtim);
    m3ApiWriteMem64(o_buf + 24, i_stat->ctim);
}

// Resolves the guest's scatter/gather list to host addresses, checking each buffer
// against guest memory, and adds up how much they ask for. The host layer is handed
// the result and never sees a guest offset.
static
const void* resolve_iovs (IM3Runtime runtime, void* _mem, m3_wasi_iovec_t* o_iovs,
                          const wasi_iovec_t* i_wasiIovs, __wasi_size_t i_count,
                          __wasi_size_t* o_want)
{
    *o_want = 0;

    __wasi_size_t want = 0;

    for (__wasi_size_t i = 0; i < i_count; i++) {
        o_iovs[i].buf = m3ApiOffsetToPtr(m3ApiReadMem32(&i_wasiIovs[i].buf));
        o_iovs[i].len = m3ApiReadMem32(&i_wasiIovs[i].buf_len);
        m3ApiCheckMem(o_iovs[i].buf, o_iovs[i].len);
        want += (__wasi_size_t)o_iovs[i].len;
    }

    *o_want = want;
    m3ApiSuccess();
}

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

    if (fd < 3 || fd >= d_m3WasiPreopenCount) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }
    size_t slen = strlen(m3_wasi_preopen[fd].path) + 1;
    memcpy(path, m3_wasi_preopen[fd].path, M3_MIN(slen, path_len));
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_prestat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(uint8_t*, buf)

    m3ApiCheckMem(buf, 8);

    if (fd < 3 || fd >= d_m3WasiPreopenCount) {
        m3ApiReturn(__WASI_ERRNO_BADF);
    }

    m3ApiWriteMem32(buf + 0, __WASI_PREOPENTYPE_DIR);
    m3ApiWriteMem32(buf + 4, (u32)strlen(m3_wasi_preopen[fd].path) + 1);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_fdstat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(__wasi_fdstat_t*, fdstat)

    m3ApiCheckMem(fdstat, sizeof(__wasi_fdstat_t));

    __wasi_filetype_t filetype;
    __wasi_fdflags_t  flags;

    __wasi_errno_t err = m3_wasi_host_fd_fdstat(fd, &filetype, &flags);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    fdstat->fs_filetype = filetype;
    m3ApiWriteMem16(&fdstat->fs_flags, flags);

    fdstat->fs_rights_base = (uint64_t)-1; // all rights

    // Make descriptors 0,1,2 look like a TTY
    if (fd <= 2) {
        fdstat->fs_rights_base &= ~(__WASI_RIGHTS_FD_SEEK | __WASI_RIGHTS_FD_TELL);
    }

    fdstat->fs_rights_inheriting = (uint64_t)-1; // all rights
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_fdstat_set_flags)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_fdflags_t, flags)

    m3ApiReturn(m3_wasi_host_fd_fdstat_set_flags(fd, flags));
}

m3ApiRawFunction(m3_wasi_generic_fd_advise)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_filesize_t, offset)
    m3ApiGetArg(__wasi_filesize_t, length)
    m3ApiGetArg(__wasi_advice_t, advice)

    m3ApiReturn(m3_wasi_host_fd_advise(fd, offset, length, advice));
}

m3ApiRawFunction(m3_wasi_unstable_fd_filestat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(uint8_t*, buf)

    m3ApiCheckMem(buf, d_m3WasiUnstableFilestatSize);

    m3_wasi_filestat_t fd_stat;

    __wasi_errno_t err = m3_wasi_host_fd_filestat(fd, &fd_stat);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    write_filestat(buf, &fd_stat, true);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_preview1_fd_filestat_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(uint8_t*, buf)

    m3ApiCheckMem(buf, d_m3WasiFilestatSize);

    m3_wasi_filestat_t fd_stat;

    __wasi_errno_t err = m3_wasi_host_fd_filestat(fd, &fd_stat);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    write_filestat(buf, &fd_stat, false);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
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

    m3_wasi_filestat_t path_stat;
    bool               follow = (dirflags & __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW) != 0;

    __wasi_errno_t err = m3_wasi_host_path_filestat(dirfd, host_path, follow, &path_stat);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    write_filestat(buf, &path_stat, true);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_preview1_path_filestat_get)
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

    m3_wasi_filestat_t path_stat;
    bool               follow = (dirflags & __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW) != 0;

    __wasi_errno_t err = m3_wasi_host_path_filestat(dirfd, host_path, follow, &path_stat);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    write_filestat(buf, &path_stat, false);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_filestat_set_size)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_filesize_t, size)

    m3ApiReturn(m3_wasi_host_fd_set_size(fd, size));
}

m3ApiRawFunction(m3_wasi_generic_fd_filestat_set_times)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArg(__wasi_timestamp_t, atim)
    m3ApiGetArg(__wasi_timestamp_t, mtim)
    m3ApiGetArg(__wasi_fstflags_t, flags)

    m3ApiReturn(m3_wasi_host_fd_set_times(fd, atim, mtim, flags));
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

    bool follow = (old_flags & __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW) != 0;

    m3ApiReturn(m3_wasi_host_path_link(old_dirfd, host_old, follow, new_dirfd, host_new));
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

    m3ApiReturn(m3_wasi_host_path_create_directory(dirfd, host_path));
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

    m3ApiReturn(m3_wasi_host_path_remove_directory(dirfd, host_path));
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

    m3ApiReturn(m3_wasi_host_path_unlink_file(dirfd, host_path));
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

    m3ApiReturn(m3_wasi_host_path_rename(old_dirfd, host_old, new_dirfd, host_new));
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

    m3ApiReturn(m3_wasi_host_path_symlink(host_old, dirfd, host_new));
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

    __wasi_size_t used;

    __wasi_errno_t err = m3_wasi_host_path_readlink(dirfd, host_path, buf, buf_len, &used);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    m3ApiWriteMem32(bufused, used);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_tell)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(__wasi_filesize_t*, result)

    m3ApiCheckMem(result, sizeof(__wasi_filesize_t));

    __wasi_filesize_t pos;

    __wasi_errno_t err = m3_wasi_host_fd_seek(fd, 0, SEEK_CUR, &pos);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    m3ApiWriteMem64(result, pos);
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

    __wasi_filesize_t pos;

    __wasi_errno_t err = m3_wasi_host_fd_seek(fd, offset, whence, &pos);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    m3ApiWriteMem64(result, pos);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_preview1_fd_seek)
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

    __wasi_filesize_t pos;

    __wasi_errno_t err = m3_wasi_host_fd_seek(fd, offset, whence, &pos);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    m3ApiWriteMem64(result, pos);
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

    char host_path[d_m3WasiMaxPath];
    if (!copy_path(host_path, path, path_len)) {
        m3ApiReturn(__WASI_ERRNO_NAMETOOLONG);
    }

    __wasi_fd_t host_fd;

    __wasi_errno_t err = m3_wasi_host_path_open(dirfd, host_path, oflags, fs_rights_base,
                                                fs_flags, &host_fd);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    m3ApiWriteMem32(fd, host_fd);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

// The four transfer calls all walk the guest's list a batch at a time - see
// d_m3WasiMaxIovs - and stop where a batch comes back short, as a short readv would
// have stopped the whole call.

m3ApiRawFunction(m3_wasi_generic_fd_read)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, wasi_iovs)
    m3ApiGetArg(__wasi_size_t, iovs_len)
    m3ApiGetArgMem(__wasi_size_t*, nread)

    m3ApiCheckMem(wasi_iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nread, sizeof(__wasi_size_t));

    m3_wasi_iovec_t iovs[d_m3WasiMaxIovs];
    __wasi_size_t   total = 0;

    for (__wasi_size_t i = 0; i < iovs_len; i += d_m3WasiMaxIovs) {
        __wasi_size_t count = M3_MIN(iovs_len - i, (__wasi_size_t)d_m3WasiMaxIovs);
        __wasi_size_t want, done;

        const void* mem_check = resolve_iovs(runtime, _mem, iovs, wasi_iovs + i, count, &want);
        if (mem_check != m3Err_none) {
            return mem_check;
        }

        __wasi_errno_t err = m3_wasi_host_fd_read(fd, iovs, count, &done);
        if (err != __WASI_ERRNO_SUCCESS) {
            m3ApiReturn(err);
        }

        total += done;
        if (done < want) {
            break;
        }
    }

    m3ApiWriteMem32(nread, total);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
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

    m3_wasi_iovec_t iovs[d_m3WasiMaxIovs];
    __wasi_size_t   total = 0;

    for (__wasi_size_t i = 0; i < iovs_len; i += d_m3WasiMaxIovs) {
        __wasi_size_t count = M3_MIN(iovs_len - i, (__wasi_size_t)d_m3WasiMaxIovs);
        __wasi_size_t want, done;

        const void* mem_check = resolve_iovs(runtime, _mem, iovs, wasi_iovs + i, count, &want);
        if (mem_check != m3Err_none) {
            return mem_check;
        }

        __wasi_errno_t err = m3_wasi_host_fd_write(fd, iovs, count, &done);
        if (err != __WASI_ERRNO_SUCCESS) {
            m3ApiReturn(err);
        }

        total += done;
        if (done < want) {
            break;
        }
    }

    m3ApiWriteMem32(nwritten, total);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

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

    m3_wasi_iovec_t iovs[d_m3WasiMaxIovs];
    __wasi_size_t   total = 0;

    for (__wasi_size_t i = 0; i < iovs_len; i += d_m3WasiMaxIovs) {
        __wasi_size_t count = M3_MIN(iovs_len - i, (__wasi_size_t)d_m3WasiMaxIovs);
        __wasi_size_t want, done;

        const void* mem_check = resolve_iovs(runtime, _mem, iovs, wasi_iovs + i, count, &want);
        if (mem_check != m3Err_none) {
            return mem_check;
        }

        // the offset walks with the data, so a short batch leaves no hole behind it
        __wasi_errno_t err = m3_wasi_host_fd_pread(fd, iovs, count, offset + total, &done);
        if (err != __WASI_ERRNO_SUCCESS) {
            m3ApiReturn(err);
        }

        total += done;
        if (done < want) {
            break;
        }
    }

    m3ApiWriteMem32(nread, total);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
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

    m3_wasi_iovec_t iovs[d_m3WasiMaxIovs];
    __wasi_size_t   total = 0;

    for (__wasi_size_t i = 0; i < iovs_len; i += d_m3WasiMaxIovs) {
        __wasi_size_t count = M3_MIN(iovs_len - i, (__wasi_size_t)d_m3WasiMaxIovs);
        __wasi_size_t want, done;

        const void* mem_check = resolve_iovs(runtime, _mem, iovs, wasi_iovs + i, count, &want);
        if (mem_check != m3Err_none) {
            return mem_check;
        }

        __wasi_errno_t err = m3_wasi_host_fd_pwrite(fd, iovs, count, offset + total, &done);
        if (err != __WASI_ERRNO_SUCCESS) {
            m3ApiReturn(err);
        }

        total += done;
        if (done < want) {
            break;
        }
    }

    m3ApiWriteMem32(nwritten, total);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_close)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)

    m3ApiReturn(m3_wasi_host_fd_close(fd));
}

m3ApiRawFunction(m3_wasi_generic_fd_datasync)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_fd_t, fd)

    m3ApiReturn(m3_wasi_host_fd_datasync(fd));
}

m3ApiRawFunction(m3_wasi_generic_random_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint8_t*, buf)
    m3ApiGetArg(__wasi_size_t, buf_len)

    m3ApiCheckMem(buf, buf_len);

    m3ApiReturn(m3_wasi_host_random_get(buf, buf_len));
}

m3ApiRawFunction(m3_wasi_generic_clock_res_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_clockid_t, wasi_clk_id)
    m3ApiGetArgMem(__wasi_timestamp_t*, resolution)

    m3ApiCheckMem(resolution, sizeof(__wasi_timestamp_t));

    __wasi_timestamp_t res;

    __wasi_errno_t err = m3_wasi_host_clock_res_get(wasi_clk_id, &res);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    m3ApiWriteMem64(resolution, res);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_clock_time_get)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(__wasi_clockid_t, wasi_clk_id)
    m3ApiGetArg(__wasi_timestamp_t, precision)
    m3ApiGetArgMem(__wasi_timestamp_t*, time)

    m3ApiCheckMem(time, sizeof(__wasi_timestamp_t));

    __wasi_timestamp_t now;

    __wasi_errno_t err = m3_wasi_host_clock_time_get(wasi_clk_id, &now);
    if (err != __WASI_ERRNO_SUCCESS) {
        m3ApiReturn(err);
    }

    m3ApiWriteMem64(time, now);
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

    m3_wasi_host_init();

    static const char* namespaces[2] = { "wasi_unstable", "wasi_snapshot_preview1" };

    // clang-format off

    // Some functions are incompatible between WASI versions
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_unstable",          "fd_seek",     "i(iIi*)", &m3_wasi_unstable_fd_seek)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_seek",     "i(iIi*)", &m3_wasi_preview1_fd_seek)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_unstable",          "fd_filestat_get",   "i(i*)",     &m3_wasi_unstable_fd_filestat_get)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_filestat_get",   "i(i*)",     &m3_wasi_preview1_fd_filestat_get)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_unstable",          "path_filestat_get", "i(ii*i*)",  &m3_wasi_unstable_path_filestat_get)));
_   (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_snapshot_preview1", "path_filestat_get", "i(ii*i*)",  &m3_wasi_preview1_path_filestat_get)));
//_ (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_unstable",          "poll_oneoff",       "i(**i*)",   &m3_wasi_unstable_poll_oneoff)));
//_ (SuppressLookupFailure(m3_LinkRawFunction(module, "wasi_snapshot_preview1", "poll_oneoff",       "i(**i*)",   &m3_wasi_preview1_poll_oneoff)));

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
