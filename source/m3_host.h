//
//  m3_host.h
//
//  What the engine asks of the system it is hosted on.
//
//  Everything here is something only the operating system can answer: how much native
//  stack this thread still has, how to get at a file's bytes, and - where the build
//  asks for guarded memories - how to reserve address space, commit part of it, and
//  turn the fault from a read past the committed part back into a Wasm trap.
//
//  Exactly one implementation is ever built:
//
//    m3_host_win32.h    VirtualAlloc/VirtualQuery, file mappings, and structured
//                       exception handling
//    m3_host_posix.h    mmap/mprotect, file mappings, and a SIGSEGV handler
//    m3_host_none.h     a system with none of that, which is most of platforms/:
//                       it reads a file rather than mapping one, and says it cannot
//                       measure a stack
//
//  All three are headers, and m3_core.c includes whichever one it is built for: a new
//  .c would have to be added to every source list that names its sources one by one,
//  and most of those are build scripts outside this repository. Unlike the WASI host
//  split, the callers here are in other translation units, so these are real
//  definitions rather than static ones - this header is the only declaration of them
//  there is.
//
//  Every call is allowed to fail, and failing is not an error: m3_HostStackBase
//  answers NULL on a system that cannot be asked, and the engine falls back to the
//  compile-time budget. Only the reservation calls report failure the caller has to
//  act on, and only a build with d_m3GuardedMemory has any.
//

#ifndef m3_host_h
#define m3_host_h

#include "m3_core.h"

d_m3BeginExternC

// The lowest address the calling thread's stack can reach, or NULL when this
// build cannot ask (see d_m3HasThreadStackProbe). What sits at the very bottom
// is the system's business - a guard page usually - so the answer bounds the
// stack rather than describing what is safe to touch; d_m3NativeStackMargin is
// what keeps the difference.
void* m3_HostStackBase (void);


// A file's bytes and how they were come by.
//
// 'mapped' is the one that says who gives the bytes back, and it is not the same
// question as whether there is a handle: a Win32 mapping keeps nothing open, because
// the view holds the section which holds the file object and the share mode with it,
// while POSIX has to hold the descriptor its lock lives on. So a mapped file may have
// no handle, and d_m3HostNoHandle is what that looks like.
#define d_m3HostNoHandle    ((intptr_t) -1)

typedef struct M3HostFile {
    void*    data;
    size_t   size;
    intptr_t handle;     // the host layer's, and d_m3HostNoHandle when there is none
    bool     mapped;
} M3HostFile;

// The module's bytes, however this system can best get at them: mapped where it can
// map, read onto the heap where it cannot - a system with no mapping at all, a
// filesystem that will not, or something that is not a file, which is what a pipe
// from a process substitution is. The caller is told which only by 'mapped', and
// needs to know only because m3_HostUnmapFile is what gives the bytes back either
// way.
//
// i_maxBytes refuses a file larger than the caller is willing to take, before
// anything is allocated for it; 0 accepts any size the system will hand over. False
// is that, or the file not being readable at all, and o_file is written on success
// only.
bool m3_HostMapFile (const char* i_path, size_t i_maxBytes, M3HostFile* o_file);

// Give the bytes back, releasing the mapping and its lock, or the heap block
void m3_HostUnmapFile (M3HostFile* io_file);

// The reading half on its own: what each m3_HostMapFile falls back to, and the whole
// of what a system with no mapping does. The same on every system, so it is here
// rather than in any of them - and inline, so that a translation unit which never
// calls it is not the one that drags stdio's FILE machinery into the link.
static inline
bool m3_HostReadFile (const char* i_path, size_t i_maxBytes, M3HostFile* o_file)
{
    FILE* f = fopen(i_path, "rb");
    if (f == NULL) {
        return false;
    }

    // The size is taken before anything is allocated, so that a file too big to be
    // wanted is refused rather than read
    fseek(f, 0, SEEK_END);
    long tell = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (tell < 0 or (i_maxBytes and (size_t) tell > i_maxBytes)) {
        fclose(f);
        return false;
    }

    size_t size = (size_t)tell;
    void*  data = m3_Malloc("Host File", size ? size : 1);

    if (data == NULL) {
        fclose(f);
        return false;
    }

    bool read = (fread(data, 1, size, f) == size);
    fclose(f);

    if (not read) {
        m3_Free(data);
        return false;
    }

    o_file->data = data;
    o_file->size = size;
    o_file->handle = d_m3HostNoHandle;
    o_file->mapped = false;
    return true;
}

// A module is parsed in place and keeps pointing into the mapped bytes for as long
// as it is loaded, so this is where a module's own size stops being paid for twice:
// what is mapped stays in the page cache, shared with anything else running the same
// file, and is only ever read as far as the module is walked.
//
// What is mapped is the file itself, so a file that changes underneath the mapping
// changes under a running module. Each system is asked to prevent that as far as it
// can, and the two can do very different amounts:
//
//   Win32   a share mode the kernel enforces. Another process opening the file for
//           writing is refused outright while the mapping lives, so this is a real
//           guarantee. Other readers - another wasm3 on the same module - are fine.
//   POSIX   a shared advisory lock, which stops only a writer that asks for a lock
//           of its own. Nothing stops a plain open-and-write, because POSIX has no
//           portable mandatory locking: Linux removed what it had in 5.15, and
//           O_EXLOCK is a BSD extension that is advisory as well.
//
// So on POSIX this narrows the window rather than closing it.

#if d_m3GuardedMemory

#  if M3_SIZEOF_PTR < 8
#    error "d_m3GuardedMemory needs a 64-bit address space"
#  endif
#  if d_m3FixedHeap
#    error "d_m3GuardedMemory and d_m3FixedHeap are two different places for a linear memory to come from"
#  endif
#  if defined(_WIN32) && !defined(_MSC_VER)
#    error "d_m3GuardedMemory on Windows needs __try/__except - build with MSVC or clang-cl"
#  endif

// The system's allocation granularity: what a reservation's size and a commit's
// bounds are rounded to. Never zero.
size_t m3_HostPageSize (void);

// Reserve i_bytes of address space without backing any of it. Reading or writing it
// faults until m3_HostCommit says otherwise, which is the whole point: the fault is
// what a Wasm access past the end of its memory turns into. NULL if the system will
// not.
void*  m3_HostReserve (size_t i_bytes);

// Back [i_address, i_address + i_bytes) with pages, which read as zero. The range
// has to be inside a reservation from m3_HostReserve; committing what is already
// committed is not an error and changes nothing.
bool   m3_HostCommit (void* i_address, size_t i_bytes);

// Undo m3_HostCommit for [i_address, i_address + i_bytes), giving the pages back to
// the system and leaving the range reserved and unreachable again. What was written
// there is gone: committing it once more reads as zero.
bool   m3_HostDecommit (void* i_address, size_t i_bytes);

// Give back a whole reservation. i_bytes is what was reserved.
void   m3_HostRelease (void* i_address, size_t i_bytes);

// Run i_body(i_context) with a fault inside [i_guardLow, i_guardLow + i_guardBytes)
// caught and reported rather than killing the process: false means the body touched
// that range and did not finish. Any other fault is left to whatever would have
// handled it, so an engine bug still looks like an engine bug.
//
// Nests, so a host function calling back into Wasm gets a region of its own, and is
// per-thread. What the body was doing when it faulted is abandoned where it stands -
// the caller has to be able to carry on without it, which for the interpreter means
// the Wasm stack and the memory, both of which are just data.
bool   m3_HostProtectedCall (void (*i_body)(void*), void* i_context,
                             void* i_guardLow, size_t i_guardBytes);

#endif // d_m3GuardedMemory

d_m3EndExternC

#endif // m3_host_h
