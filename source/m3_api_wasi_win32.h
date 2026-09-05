//
//  m3_api_wasi_win32.h
//
//  The Win32 implementation of m3_api_wasi_host.h. Windows has no *at() family, no
//  fstat that names a file, no positional read or write and no readlink, so a path
//  is resolved by writing the preopen's real path in front of it and the rest is
//  assembled out of the Win32 API.
//
//  A header rather than a translation unit of its own, and included by m3_api_wasi.c
//  alone: a new .c would have to be added to every source list that names them one by
//  one, and most of those are in build scripts outside this repository.
//

#ifndef m3_api_wasi_win32_h
#define m3_api_wasi_win32_h

#include "m3_core.h"

#if !defined(_WIN32)
#  error "This file is for Windows only"
#endif

#include <Windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <wchar.h>

// See http://msdn.microsoft.com/en-us/library/windows/desktop/aa387694.aspx
#define SystemFunction036 NTAPI SystemFunction036
#include <NTSecAPI.h>
#undef SystemFunction036

#include "m3_api_wasi_host.h"

// Room for a preopen's real path in front of the guest's
#define d_m3WasiMaxHostPath  (d_m3WasiMaxPath + 16)

// 1601-01-01, where a FILETIME counts its 100ns ticks from, in those same ticks
#define d_m3WinEpochOffset   116444736000000000ULL

// Only the Windows 10 SDK and later declare it
#if !defined(SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
#  define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE  0x2
#endif

// A reparse point is read through an ioctl whose reply is described in ntifs.h,
// which does not ship with the compiler, so the layout is spelled out below. The two
// tags that name a path differ only in the symbolic link's extra Flags word.
#if !defined(FSCTL_GET_REPARSE_POINT)
#  define FSCTL_GET_REPARSE_POINT  0x000900A8
#endif
#if !defined(MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
#  define MAXIMUM_REPARSE_DATA_BUFFER_SIZE  (16 * 1024)
#endif

typedef struct ReparseDataBuffer {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    union {
        struct {
            ULONG Flags;
            WCHAR PathBuffer[1];
        } symlink;
        struct {
            WCHAR PathBuffer[1];
        } mount_point;
    } u;
} ReparseDataBuffer;


/*
 * Plumbing
 */

// What the CRT calls used here leave in errno. Everything the Win32 API reports goes
// through errno_from_win32 instead.
static
__wasi_errno_t errno_from_crt (int err)
{
    // clang-format off
    switch (err) {
    case 0:       return __WASI_ERRNO_SUCCESS;
    case EACCES:  return __WASI_ERRNO_ACCES;
    case EBADF:   return __WASI_ERRNO_BADF;
    case EEXIST:  return __WASI_ERRNO_EXIST;
    case EMFILE:  return __WASI_ERRNO_MFILE;
    case ENOENT:  return __WASI_ERRNO_NOENT;
    case ENOSPC:  return __WASI_ERRNO_NOSPC;
    case EINVAL:  return __WASI_ERRNO_INVAL;
    }
    // clang-format on
    return __WASI_ERRNO_IO;
}

static
__wasi_errno_t errno_from_win32 (DWORD err)
{
    // clang-format off
    switch (err) {
    case ERROR_SUCCESS:               return __WASI_ERRNO_SUCCESS;
    case ERROR_INVALID_FUNCTION:      return __WASI_ERRNO_NOTSUP;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_PATHNAME:          return __WASI_ERRNO_NOENT;
    case ERROR_TOO_MANY_OPEN_FILES:   return __WASI_ERRNO_MFILE;
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:         return __WASI_ERRNO_ACCES;
    case ERROR_INVALID_HANDLE:        return __WASI_ERRNO_BADF;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:           return __WASI_ERRNO_NOMEM;
    case ERROR_NOT_SAME_DEVICE:       return __WASI_ERRNO_XDEV;
    case ERROR_WRITE_FAULT:
    case ERROR_READ_FAULT:            return __WASI_ERRNO_IO;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:        return __WASI_ERRNO_BUSY;
    case ERROR_HANDLE_DISK_FULL:
    case ERROR_DISK_FULL:             return __WASI_ERRNO_NOSPC;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:        return __WASI_ERRNO_EXIST;
    case ERROR_BUFFER_OVERFLOW:
    case ERROR_INSUFFICIENT_BUFFER:
    case ERROR_FILENAME_EXCED_RANGE:  return __WASI_ERRNO_NAMETOOLONG;
    case ERROR_DIR_NOT_EMPTY:         return __WASI_ERRNO_NOTEMPTY;
    case ERROR_DIRECTORY:             return __WASI_ERRNO_NOTDIR;
    case ERROR_PRIVILEGE_NOT_HELD:    return __WASI_ERRNO_PERM;
    case ERROR_NOT_SUPPORTED:         return __WASI_ERRNO_NOTSUP;
    case ERROR_CANT_RESOLVE_FILENAME: return __WASI_ERRNO_LOOP;
    case ERROR_NEGATIVE_SEEK:
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER:
    case ERROR_NOT_A_REPARSE_POINT:   return __WASI_ERRNO_INVAL;
    }
    // clang-format on
    return __WASI_ERRNO_IO;
}

static
__wasi_timestamp_t convert_filetime (FILETIME i_time)
{
    uint64_t ticks = ((uint64_t)i_time.dwHighDateTime << 32) | i_time.dwLowDateTime;
    if (ticks < d_m3WinEpochOffset) {
        return 0;
    }
    return (ticks - d_m3WinEpochOffset) * 100;
}

static
FILETIME convert_timestamp (__wasi_timestamp_t i_time)
{
    uint64_t ticks = i_time / 100 + d_m3WinEpochOffset;
    FILETIME out;
    out.dwLowDateTime = (DWORD)ticks;
    out.dwHighDateTime = (DWORD)(ticks >> 32);
    return out;
}

// Whether i_fd is one of the preopened directories. A preopen holds a placeholder
// descriptor rather than an open directory, so it has to answer for itself wherever
// a stat is asked for, and it names a directory only while the placeholder opened
// for it still holds that number.
static
bool is_preopen (__wasi_fd_t i_fd)
{
    return i_fd >= 3 && i_fd < d_m3WasiPreopenCount && m3_wasi_preopen[i_fd].fd == (int)i_fd;
}

// A preopened directory is resolved by prefixing its real path onto the guest's,
// which is what openat() would have resolved the pair to.
static
__wasi_errno_t resolve_path (char* o_path, __wasi_fd_t i_dirfd, ccstr_t i_path)
{
    if (!is_preopen(i_dirfd)) {
        return __WASI_ERRNO_BADF;
    }

    int len = snprintf(o_path, d_m3WasiMaxHostPath, "%s/%s", m3_wasi_preopen[i_dirfd].real_path,
                       i_path);
    if (len < 0 || len >= d_m3WasiMaxHostPath) {
        return __WASI_ERRNO_NAMETOOLONG;
    }
    return __WASI_ERRNO_SUCCESS;
}

// FILE_FLAG_BACKUP_SEMANTICS is what lets a directory be opened at all, and
// FILE_READ_ATTRIBUTES is all the metadata calls need, so this opens whatever the
// path names without asking for access that could be refused.
static
__wasi_errno_t open_path_handle (ccstr_t i_path, bool i_follow, HANDLE* o_handle)
{
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (!i_follow) {
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    }

    HANDLE handle = CreateFileA(i_path, FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, flags, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return errno_from_win32(GetLastError());
    }

    *o_handle = handle;
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t handle_for_fd (__wasi_fd_t i_fd, HANDLE* o_handle)
{
    HANDLE handle = (HANDLE)_get_osfhandle(i_fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return __WASI_ERRNO_BADF;
    }

    *o_handle = handle;
    return __WASI_ERRNO_SUCCESS;
}

// A handle that is not a file - a console, a pipe - carries none of this and answers
// with its type alone. Windows records no change time, so ctim is the creation time,
// the nearest thing it has.
static
__wasi_errno_t filestat_from_handle (HANDLE i_handle, m3_wasi_filestat_t* o_stat)
{
    memset(o_stat, 0, sizeof(*o_stat));

    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(i_handle, &info)) {
        switch (GetFileType(i_handle)) {
        case FILE_TYPE_CHAR: o_stat->filetype = __WASI_FILETYPE_CHARACTER_DEVICE; break;
        case FILE_TYPE_PIPE: o_stat->filetype = __WASI_FILETYPE_UNKNOWN; break;
        default: return errno_from_win32(GetLastError());
        }
        return __WASI_ERRNO_SUCCESS;
    }

    o_stat->dev = info.dwVolumeSerialNumber;
    o_stat->ino = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    o_stat->nlink = info.nNumberOfLinks;
    o_stat->size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    o_stat->atim = convert_filetime(info.ftLastAccessTime);
    o_stat->mtim = convert_filetime(info.ftLastWriteTime);
    o_stat->ctim = convert_filetime(info.ftCreationTime);

    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        o_stat->filetype = __WASI_FILETYPE_SYMBOLIC_LINK;
    } else if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        o_stat->filetype = __WASI_FILETYPE_DIRECTORY;
    } else {
        o_stat->filetype = __WASI_FILETYPE_REGULAR_FILE;
    }
    return __WASI_ERRNO_SUCCESS;
}


/*
 * Setup
 */

static
void m3_wasi_host_init ()
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

    // Windows cannot open a directory as a descriptor, so a preopen is resolved by
    // path instead. It still has to hold a descriptor number: the guest is handed
    // one, and without something occupying it the next file opened would be given
    // the same number and the two would be indistinguishable.
    for (int i = 3; i < d_m3WasiPreopenCount; i++) {
        if (m3_wasi_preopen[i].fd < 0) {
            m3_wasi_preopen[i].fd = _open("NUL", _O_RDONLY);
        }
    }
}


/*
 * Descriptors
 */

static
__wasi_errno_t m3_wasi_host_fd_close (__wasi_fd_t i_fd)
{
    if (_close(i_fd) != 0) {
        return errno_from_crt(errno);
    }
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_fd_datasync (__wasi_fd_t i_fd)
{
    if (_commit(i_fd) != 0) {
        return errno_from_crt(errno);
    }
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_fd_advise (__wasi_fd_t i_fd, __wasi_filesize_t i_offset,
                                       __wasi_filesize_t i_length, __wasi_advice_t i_advice)
{
    // Purely advisory, and Windows has nowhere to pass it on to, so dropping it is
    // still a conforming implementation. Only the fd has to be valid.
    if (i_advice > __WASI_ADVICE_NOREUSE) {
        return __WASI_ERRNO_INVAL;
    }

    HANDLE handle;
    return handle_for_fd(i_fd, &handle);
}

static
__wasi_errno_t m3_wasi_host_fd_filestat (__wasi_fd_t i_fd, m3_wasi_filestat_t* o_stat)
{
    memset(o_stat, 0, sizeof(*o_stat));

    if (is_preopen(i_fd)) {
        o_stat->filetype = __WASI_FILETYPE_DIRECTORY;
        return __WASI_ERRNO_SUCCESS;
    }

    HANDLE         handle;
    __wasi_errno_t err = handle_for_fd(i_fd, &handle);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    return filestat_from_handle(handle, o_stat);
}

static
__wasi_errno_t m3_wasi_host_fd_fdstat (__wasi_fd_t i_fd, __wasi_filetype_t* o_filetype,
                                       __wasi_fdflags_t* o_flags)
{
    *o_filetype = __WASI_FILETYPE_UNKNOWN;
    *o_flags = 0;

    m3_wasi_filestat_t fd_stat;

    __wasi_errno_t     err = m3_wasi_host_fd_filestat(i_fd, &fd_stat);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    *o_filetype = fd_stat.filetype;
    *o_flags = 0; // the CRT does not report back the flags a descriptor was opened with
    return __WASI_ERRNO_SUCCESS;
}

// Nothing here can be changed after the fact: append and non-blocking are fixed when
// CreateFile runs, and the CRT hands back no way to revisit either. fd_fdstat above
// reports no flags at all for the same reason, so the only request that can be
// honoured is the one asking for exactly that.
static
__wasi_errno_t m3_wasi_host_fd_fdstat_set_flags (__wasi_fd_t i_fd, __wasi_fdflags_t i_flags)
{
    HANDLE         handle;
    __wasi_errno_t err = handle_for_fd(i_fd, &handle);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    return (i_flags == 0) ? __WASI_ERRNO_SUCCESS : __WASI_ERRNO_NOTSUP;
}

static
__wasi_errno_t m3_wasi_host_fd_set_size (__wasi_fd_t i_fd, __wasi_filesize_t i_size)
{
    // _chsize_s hands the error back rather than only leaving it in errno
    return errno_from_crt(_chsize_s(i_fd, (__int64)i_size));
}

static
__wasi_errno_t m3_wasi_host_fd_set_times (__wasi_fd_t i_fd, __wasi_timestamp_t i_atim,
                                          __wasi_timestamp_t i_mtim, __wasi_fstflags_t i_flags)
{
    HANDLE         handle;
    __wasi_errno_t err = handle_for_fd(i_fd, &handle);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    // a time is either given, asked for as now, or left alone - and SetFileTime
    // leaves alone whatever it is handed no time for
    FILETIME now, times[2];
    GetSystemTimeAsFileTime(&now);

    const FILETIME* atime = NULL;
    const FILETIME* mtime = NULL;

    if (i_flags & __WASI_FSTFLAGS_ATIM_NOW) {
        atime = &now;
    } else if (i_flags & __WASI_FSTFLAGS_ATIM) {
        times[0] = convert_timestamp(i_atim);
        atime = &times[0];
    }

    if (i_flags & __WASI_FSTFLAGS_MTIM_NOW) {
        mtime = &now;
    } else if (i_flags & __WASI_FSTFLAGS_MTIM) {
        times[1] = convert_timestamp(i_mtim);
        mtime = &times[1];
    }

    if (!SetFileTime(handle, NULL, atime, mtime)) {
        return errno_from_win32(GetLastError());
    }
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_fd_seek (__wasi_fd_t i_fd, int64_t i_offset, int i_whence,
                                     __wasi_filesize_t* o_pos)
{
    *o_pos = 0;

    int64_t ret = _lseeki64(i_fd, i_offset, i_whence);
    if (ret < 0) {
        return errno_from_crt(errno);
    }

    *o_pos = (__wasi_filesize_t)ret;
    return __WASI_ERRNO_SUCCESS;
}


/*
 * Transfers
 */

// Windows has neither a vectored nor a positional transfer, so the list is walked
// one buffer at a time and a short transfer ends it, as it would for readv. A
// positional one moves the file pointer and puts it back where it was.
static
__wasi_errno_t transfer_iovs (__wasi_fd_t i_fd, const m3_wasi_iovec_t* i_iovs,
                              __wasi_size_t i_iovsLen, __wasi_filesize_t i_offset,
                              bool i_positional, bool i_write, __wasi_size_t* o_done)
{
    *o_done = 0;

    int64_t saved = 0;

    if (i_positional) {
        saved = _lseeki64(i_fd, 0, SEEK_CUR);
        if (saved < 0 || _lseeki64(i_fd, (__int64)i_offset, SEEK_SET) < 0) {
            return errno_from_crt(errno);
        }
    }

    __wasi_errno_t err = __WASI_ERRNO_SUCCESS;
    size_t         done = 0;

    for (__wasi_size_t i = 0; i < i_iovsLen; i++) {
        void*  addr = i_iovs[i].buf;
        size_t len = i_iovs[i].len;
        if (len == 0) {
            continue;
        }

        int ret = i_write ? _write(i_fd, addr, (unsigned)len) : _read(i_fd, addr, (unsigned)len);
        if (ret < 0) {
            err = errno_from_crt(errno);
            break;
        }

        done += (size_t)ret;
        if ((size_t)ret < len) {
            break;
        }
    }

    if (i_positional) {
        _lseeki64(i_fd, saved, SEEK_SET);
    }

    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    *o_done = (__wasi_size_t)done;
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_fd_read (__wasi_fd_t i_fd, const m3_wasi_iovec_t* i_iovs,
                                     __wasi_size_t i_iovsLen, __wasi_size_t* o_done)
{
    return transfer_iovs(i_fd, i_iovs, i_iovsLen, 0, false, false, o_done);
}

static
__wasi_errno_t m3_wasi_host_fd_write (__wasi_fd_t i_fd, const m3_wasi_iovec_t* i_iovs,
                                      __wasi_size_t i_iovsLen, __wasi_size_t* o_done)
{
    return transfer_iovs(i_fd, i_iovs, i_iovsLen, 0, false, true, o_done);
}

static
__wasi_errno_t m3_wasi_host_fd_pread (__wasi_fd_t i_fd, const m3_wasi_iovec_t* i_iovs,
                                      __wasi_size_t i_iovsLen, __wasi_filesize_t i_offset,
                                      __wasi_size_t* o_done)
{
    return transfer_iovs(i_fd, i_iovs, i_iovsLen, i_offset, true, false, o_done);
}

static
__wasi_errno_t m3_wasi_host_fd_pwrite (__wasi_fd_t i_fd, const m3_wasi_iovec_t* i_iovs,
                                       __wasi_size_t i_iovsLen, __wasi_filesize_t i_offset,
                                       __wasi_size_t* o_done)
{
    return transfer_iovs(i_fd, i_iovs, i_iovsLen, i_offset, true, true, o_done);
}


/*
 * Paths
 */

static
__wasi_errno_t m3_wasi_host_path_open (__wasi_fd_t i_dirfd, ccstr_t i_path,
                                       __wasi_oflags_t i_oflags, __wasi_rights_t i_rights,
                                       __wasi_fdflags_t i_fdFlags, __wasi_fd_t* o_fd)
{
    *o_fd = 0;

    char           full_path[d_m3WasiMaxHostPath];
    __wasi_errno_t err = resolve_path(full_path, i_dirfd, i_path);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    // _open refuses a directory, so one is opened through the Win32 call that can
    // and handed to the CRT afterwards. The descriptor is good for what WASI does
    // with a directory - naming it in a later path call - and for nothing else.
    if (i_oflags & __WASI_OFLAGS_DIRECTORY) {
        HANDLE handle;
        err = open_path_handle(full_path, true, &handle);
        if (err != __WASI_ERRNO_SUCCESS) {
            return err;
        }

        int dir_fd = _open_osfhandle((intptr_t)handle, _O_RDONLY);
        if (dir_fd < 0) {
            CloseHandle(handle);
            return __WASI_ERRNO_MFILE;
        }

        *o_fd = (__wasi_fd_t)dir_fd;
        return __WASI_ERRNO_SUCCESS;
    }

    // Windows has no O_DSYNC/O_RSYNC/O_SYNC and no non-blocking file descriptor, so
    // those fs_flags are dropped rather than translated.
    int flags = ((i_oflags & __WASI_OFLAGS_CREAT) ? _O_CREAT : 0) |
                ((i_oflags & __WASI_OFLAGS_EXCL) ? _O_EXCL : 0) |
                ((i_oflags & __WASI_OFLAGS_TRUNC) ? _O_TRUNC : 0) |
                ((i_fdFlags & __WASI_FDFLAGS_APPEND) ? _O_APPEND : 0) |
                _O_BINARY;

    if ((i_rights & __WASI_RIGHTS_FD_READ) && (i_rights & __WASI_RIGHTS_FD_WRITE)) {
        flags |= _O_RDWR;
    } else if ((i_rights & __WASI_RIGHTS_FD_WRITE)) {
        flags |= _O_WRONLY;
    } else if ((i_rights & __WASI_RIGHTS_FD_READ)) {
        flags |= _O_RDONLY; // no-op because O_RDONLY is 0
    }

    int host_fd = _open(full_path, flags, _S_IREAD | _S_IWRITE);
    if (host_fd < 0) {
        return errno_from_crt(errno);
    }

    *o_fd = (__wasi_fd_t)host_fd;
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_path_filestat (__wasi_fd_t i_dirfd, ccstr_t i_path, bool i_follow,
                                           m3_wasi_filestat_t* o_stat)
{
    memset(o_stat, 0, sizeof(*o_stat));

    char           full_path[d_m3WasiMaxHostPath];
    __wasi_errno_t err = resolve_path(full_path, i_dirfd, i_path);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    HANDLE handle;
    err = open_path_handle(full_path, i_follow, &handle);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    err = filestat_from_handle(handle, o_stat);
    CloseHandle(handle);
    return err;
}

static
__wasi_errno_t m3_wasi_host_path_create_directory (__wasi_fd_t i_dirfd, ccstr_t i_path)
{
    char           full_path[d_m3WasiMaxHostPath];
    __wasi_errno_t err = resolve_path(full_path, i_dirfd, i_path);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    if (!CreateDirectoryA(full_path, NULL)) {
        return errno_from_win32(GetLastError());
    }
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_path_remove_directory (__wasi_fd_t i_dirfd, ccstr_t i_path)
{
    char           full_path[d_m3WasiMaxHostPath];
    __wasi_errno_t err = resolve_path(full_path, i_dirfd, i_path);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    if (!RemoveDirectoryA(full_path)) {
        return errno_from_win32(GetLastError());
    }
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_path_unlink_file (__wasi_fd_t i_dirfd, ccstr_t i_path)
{
    char           full_path[d_m3WasiMaxHostPath];
    __wasi_errno_t err = resolve_path(full_path, i_dirfd, i_path);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    // A symbolic link to a directory is a directory entry as far as Windows is
    // concerned, so removing the link itself goes through RemoveDirectory. Every
    // other kind of directory is DeleteFile's to refuse.
    DWORD attrs = GetFileAttributesA(full_path);
    bool  isDirLink =
      attrs != INVALID_FILE_ATTRIBUTES &&
      (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ==
        (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT);

    if (!(isDirLink ? RemoveDirectoryA(full_path) : DeleteFileA(full_path))) {
        return errno_from_win32(GetLastError());
    }
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_path_rename (__wasi_fd_t i_oldDirfd, ccstr_t i_oldPath,
                                         __wasi_fd_t i_newDirfd, ccstr_t i_newPath)
{
    char           full_old[d_m3WasiMaxHostPath];
    char           full_new[d_m3WasiMaxHostPath];
    __wasi_errno_t err = resolve_path(full_old, i_oldDirfd, i_oldPath);
    if (err == __WASI_ERRNO_SUCCESS) {
        err = resolve_path(full_new, i_newDirfd, i_newPath);
    }
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    // rename() would refuse an existing destination; renameat() replaces it
    if (!MoveFileExA(full_old, full_new, MOVEFILE_REPLACE_EXISTING)) {
        return errno_from_win32(GetLastError());
    }
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_path_link (__wasi_fd_t i_oldDirfd, ccstr_t i_oldPath, bool i_follow,
                                       __wasi_fd_t i_newDirfd, ccstr_t i_newPath)
{
    char           full_old[d_m3WasiMaxHostPath];
    char           full_new[d_m3WasiMaxHostPath];
    __wasi_errno_t err = resolve_path(full_old, i_oldDirfd, i_oldPath);
    if (err == __WASI_ERRNO_SUCCESS) {
        err = resolve_path(full_new, i_newDirfd, i_newPath);
    }
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    // CreateHardLink resolves the target the way AT_SYMLINK_FOLLOW asks for and has
    // no way of not doing so, so i_follow cannot be honoured here
    if (!CreateHardLinkA(full_new, full_old, NULL)) {
        return errno_from_win32(GetLastError());
    }
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_path_symlink (ccstr_t i_target, __wasi_fd_t i_dirfd, ccstr_t i_path)
{
    char           full_new[d_m3WasiMaxHostPath];
    __wasi_errno_t err = resolve_path(full_new, i_dirfd, i_path);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    // Windows settles at creation time whether a link names a file or a directory,
    // so the target has to be looked at first. It is stored as given, which puts it
    // beside the link rather than under the preopen.
    char   target[d_m3WasiMaxHostPath];
    size_t dir_len = 0;
    for (size_t i = 0; full_new[i]; i++) {
        if (full_new[i] == '/' || full_new[i] == '\\') {
            dir_len = i + 1;
        }
    }

    int target_len = snprintf(target, sizeof(target), "%.*s%s", (int)dir_len, full_new, i_target);
    if (target_len < 0 || target_len >= (int)sizeof(target)) {
        return __WASI_ERRNO_NAMETOOLONG;
    }

    DWORD flags = 0;
    DWORD attrs = GetFileAttributesA(target);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    }

    // Creating a link is a privilege, waived only for a machine in Developer Mode
    // and only for callers that ask; Windows before 1703 rejects the asking.
    if (!CreateSymbolicLinkA(full_new, i_target,
                             flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        if (GetLastError() != ERROR_INVALID_PARAMETER ||
            !CreateSymbolicLinkA(full_new, i_target, flags)) {
            return errno_from_win32(GetLastError());
        }
    }
    return __WASI_ERRNO_SUCCESS;
}

// The link's own target, as UTF-8. The print name is the form the link was written
// with; the substitute name is the resolved one, kept as the fallback for a link that
// carries no print name, minus the object-namespace prefix Windows puts on it.
static
__wasi_errno_t read_link_target (ccstr_t i_path, char* o_target, int i_size, int* o_len)
{
    HANDLE handle = CreateFileA(i_path, FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return errno_from_win32(GetLastError());
    }

    ReparseDataBuffer* reparse =
      (ReparseDataBuffer*)m3_Malloc("ReparseDataBuffer", MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    if (!reparse) {
        CloseHandle(handle);
        return __WASI_ERRNO_NOMEM;
    }

    __wasi_errno_t err = __WASI_ERRNO_SUCCESS;
    DWORD          returned;
    if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0, reparse,
                         MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &returned, NULL)) {
        err = errno_from_win32(GetLastError());
    }
    CloseHandle(handle);

    if (err == __WASI_ERRNO_SUCCESS) {
        const WCHAR* names;
        if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
            names = reparse->u.symlink.PathBuffer;
        } else if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
            names = reparse->u.mount_point.PathBuffer;
        } else {
            names = NULL;
            err = __WASI_ERRNO_INVAL;
        }

        if (names) {
            const WCHAR* name = names + reparse->PrintNameOffset / sizeof(WCHAR);
            int          len = reparse->PrintNameLength / sizeof(WCHAR);

            if (len == 0) {
                name = names + reparse->SubstituteNameOffset / sizeof(WCHAR);
                len = reparse->SubstituteNameLength / sizeof(WCHAR);
                if (len >= 4 && wcsncmp(name, L"\\??\\", 4) == 0) {
                    name += 4;
                    len -= 4;
                }
            }

            *o_len = WideCharToMultiByte(CP_UTF8, 0, name, len, o_target, i_size, NULL, NULL);
            if (*o_len <= 0 && len != 0) {
                err = errno_from_win32(GetLastError());
            }
        }
    }

    m3_Free(reparse);
    return err;
}

static
__wasi_errno_t m3_wasi_host_path_readlink (__wasi_fd_t i_dirfd, ccstr_t i_path, char* o_buf,
                                           __wasi_size_t i_bufLen, __wasi_size_t* o_bufUsed)
{
    *o_bufUsed = 0;

    char           full_path[d_m3WasiMaxHostPath];
    __wasi_errno_t err = resolve_path(full_path, i_dirfd, i_path);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    char target[d_m3WasiMaxHostPath];
    int  target_len = 0;

    err = read_link_target(full_path, target, (int)sizeof(target), &target_len);
    if (err != __WASI_ERRNO_SUCCESS) {
        return err;
    }

    __wasi_size_t copied = M3_MIN((__wasi_size_t)target_len, i_bufLen);
    memcpy(o_buf, target, copied);
    *o_bufUsed = copied;
    return __WASI_ERRNO_SUCCESS;
}


/*
 * Clocks and entropy
 */

// Windows offers the one clock, so every WASI clock maps onto it and none of them is
// rejected
static
__wasi_errno_t m3_wasi_host_clock_res_get (__wasi_clockid_t i_clockId, __wasi_timestamp_t* o_res)
{
    // a FILETIME counts in 100ns, but the system clock behind it moves on the timer
    // tick, so this says a millisecond rather than what it can spell
    *o_res = 1000000;
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_clock_time_get (__wasi_clockid_t i_clockId, __wasi_timestamp_t* o_time)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);

    *o_time = convert_filetime(now);
    return __WASI_ERRNO_SUCCESS;
}

static
__wasi_errno_t m3_wasi_host_random_get (void* o_buf, __wasi_size_t i_len)
{
    if (!RtlGenRandom(o_buf, i_len)) {
        return __WASI_ERRNO_IO;
    }
    return __WASI_ERRNO_SUCCESS;
}

#endif // m3_api_wasi_win32_h
