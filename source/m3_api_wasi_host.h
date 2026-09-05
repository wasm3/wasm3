//
//  m3_api_wasi_host.h
//
//  What the simple WASI implementation asks of the system underneath it.
//
//  m3_api_wasi.c is the WASI interface itself: it decodes the guest's arguments,
//  bounds-checks every pointer against guest memory, and writes results back in the
//  layout the calling WASI revision asks for. Everything below that - descriptors,
//  paths, clocks, entropy - is one of the implementations of this header, of which
//  exactly one is ever built:
//
//    m3_api_wasi_posix.h    openat, fstatat, readv and the rest of POSIX
//    m3_api_wasi_win32.h    the Win32 API, which has none of those
//
//  Both are headers, and m3_api_wasi.c includes whichever one it is built for: a new
//  .c would have to be added to every source list that names its sources one by one,
//  and most of those are build scripts outside this repository. That leaves the whole
//  of it in one translation unit, so this header carries the types the two sides
//  agree on and nothing else - every m3_wasi_host_* call is static, and the
//  implementation that is built is the only declaration of it there is.
//
//  The two know nothing of each other. Neither touches guest memory: a path arrives
//  NUL-terminated, a buffer arrives already checked, and a result comes back in host
//  types, which leaves the bounds checking in the one place that can do it.
//
//  Every one of those calls writes each of its out-parameters on every path it can
//  return by, and only a call that returns __WASI_ERRNO_SUCCESS writes anything worth
//  reading. Sharing a translation unit with the caller is what makes that worth
//  saying: a value left behind on an error path is one the compiler will warn about.
//

#ifndef m3_api_wasi_host_h
#define m3_api_wasi_host_h

#include "m3_core.h"

#if defined(d_m3HasWASI)

// Fixup wasi_core.h
#  if defined(M3_COMPILER_MSVC)
#    define _Static_assert(...)
#    define __attribute__(...)
#    define _Noreturn
#  endif

#  include "extra/wasi_core.h"

#  include <stdio.h> // SEEK_SET and friends, which fd_seek is asked in

d_m3BeginExternC

// Guest paths arrive as (pointer, length); the calls below want them NUL-terminated,
// so they are copied out into a buffer of this size.
#  define d_m3WasiMaxPath  512

// The preopened directories the guest is offered. The names are what it sees and are
// the same wherever it runs; the descriptor each one holds is the host's to fill in,
// and what sits behind that descriptor is the host's business - POSIX opens the
// directory, Win32 cannot and keeps a placeholder.
#  define d_m3WasiPreopenCount  5

typedef struct m3_wasi_preopen_t {
    int     fd;
    ccstr_t path;
    ccstr_t real_path;
} m3_wasi_preopen_t;

// Defined here rather than declared because this header has exactly one includer
static m3_wasi_preopen_t m3_wasi_preopen[d_m3WasiPreopenCount] = {
    { 0,  "<stdin>",  ""  },
    { 1,  "<stdout>", ""  },
    { 2,  "<stderr>", ""  },
    { -1, "/",        "." },
    { -1, "./",       "." },
};

// A filestat as the host can fill one in, before it is written out in whichever
// layout the calling WASI revision asks for
typedef struct m3_wasi_filestat_t {
    uint64_t           dev;
    uint64_t           ino;
    uint64_t           nlink;
    uint64_t           size;
    __wasi_timestamp_t atim;
    __wasi_timestamp_t mtim;
    __wasi_timestamp_t ctim;
    __wasi_filetype_t  filetype;
} m3_wasi_filestat_t;

// A scatter/gather list the caller has already resolved to host addresses and
// checked against guest memory.
//
// The guest may name any number of buffers, and the caller hands them over this many
// at a time: a fixed batch is what keeps both sides free of an allocation and of a
// variable length array, which MSVC has not got. A short transfer ends the whole
// call, exactly as it would have within one batch, so batching changes nothing the
// guest can observe - and one batch covers every list a C library actually writes.
#  define d_m3WasiMaxIovs  8

typedef struct m3_wasi_iovec_t {
    void*  buf;
    size_t len;
} m3_wasi_iovec_t;


d_m3EndExternC

#endif // d_m3HasWASI

#endif // m3_api_wasi_host_h
