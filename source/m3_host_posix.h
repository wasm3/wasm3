//
//  m3_host_posix.h
//
//  The POSIX implementation of m3_host.h.
//
//  A header rather than a translation unit of its own, and included by m3_core.c
//  alone: a new .c would have to be added to every source list that names them one by
//  one, and most of those are in build scripts outside this repository.
//

#ifndef m3_host_posix_h
#define m3_host_posix_h

#include "m3_core.h"

#if defined(_WIN32)
#  error "This file is not for Windows"
#endif

#include "m3_host.h"

#include <fcntl.h>
#include <sys/file.h>       // flock, which m3_HostMapFile takes a shared one with
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Whether pthread_getattr_np - or Darwin's pair of calls - can be reached without
// asking the build to link a threading library it may not be linking. glibc moved
// libpthread into libc in 2.34; before that this call was the one thing here that
// would not link, so a build on an older glibc keeps the compile-time budget unless
// it says otherwise. Decided here rather than in m3_config_platforms.h because
// __GLIBC__ only exists once a libc header has been seen, which m3_core.h above is.
#ifndef d_m3HasThreadStackProbe
#  if defined(__APPLE__) || defined(__ANDROID__)
#    define d_m3HasThreadStackProbe            1    // libSystem, bionic
#  elif defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#    if __GLIBC_PREREQ(2, 34)
#      define d_m3HasThreadStackProbe          1
#    else
#      define d_m3HasThreadStackProbe          0    // still in libpthread here
#    endif
#  elif defined(__linux__)
#    define d_m3HasThreadStackProbe            1    // musl keeps it in libc
#  else
#    define d_m3HasThreadStackProbe            0
#  endif
#endif

#if d_m3HasThreadStackProbe
#  include <pthread.h>
#  if defined(__linux__)
#    include <sys/resource.h>
#    include <sys/syscall.h>
#  endif
#endif

#if d_m3HasThreadStackProbe && defined(__linux__)

// The first thread's stack, asked of the kernel rather than of pthreads, because
// pthreads is not always cheap about it: musl finds that stack by probing for its
// end a page at a time - a system call per page, and there can be a great many -
// where this is one line of text and a getrlimit. A thread given a stack of its own
// is left to pthreads, which has that one written down and answers at once.
//
// /proc says where the stack started, RLIMIT_STACK how far down it may grow. Another
// mapping can stop it growing that far, which this does not go looking for: the
// answer is then a stack larger than the real one, the budget stays what the build
// asked for, and that is what a build unable to measure gets anyway.
static
void* linux_main_stack_base (void)
{
    if ((long)syscall(SYS_gettid) != (long)getpid()) {
        return NULL;
    }

    struct rlimit limit;

    if (getrlimit(RLIMIT_STACK, &limit) != 0 or
        limit.rlim_cur == RLIM_INFINITY or limit.rlim_cur == 0) {
        return NULL;
    }

    int fd = open("/proc/self/stat", O_RDONLY);

    if (fd < 0) {
        return NULL;
    }

    char    line[512];
    ssize_t got = read(fd, line, sizeof(line) - 1);

    close(fd);

    if (got <= 0) {
        return NULL;
    }

    line[got] = 0;

    // Field 28 is where the stack started. The fields before it include the
    // executable's name in parentheses, and that name may hold anything at all -
    // spaces and parentheses included - so the count begins after the last of those.
    const char* field = strrchr(line, ')');

    for (int number = 2; field and number < 28; ++number) {
        field = strchr(field + 1, ' ');
    }

    if (field == NULL) {
        return NULL;
    }

    unsigned long long top = strtoull(field + 1, NULL, 10);

    // A limit reaching below nothing is not a stack this can describe
    if (top == 0 or (unsigned long long) limit.rlim_cur > top) {
        return NULL;
    }

    return (u8*)(uintptr_t)top - (size_t)limit.rlim_cur;
}

#endif

// Answers for every thread and not just the first, which is what asking pthreads is
// for: RLIMIT_STACK describes the stack the process started on, and a thread given a
// stack of its own is not it. Where the first thread can be recognized, it is asked
// about separately - see linux_main_stack_base.
void* m3_HostStackBase (void)
{
#if !d_m3HasThreadStackProbe

    return NULL;

#elif defined(__APPLE__)

    // Darwin describes a stack by its high end and its size, the opposite way round
    pthread_t self = pthread_self();

    u8*       high = (u8*)pthread_get_stackaddr_np(self);
    size_t    size = pthread_get_stacksize_np(self);

    if (high == NULL or size == 0) {
        return NULL;
    }

    return high - size;

#else

#  if defined(__linux__)

    // Only where it answers: a system with no /proc, or a stack with no limit on how
    // far it grows, falls through to pthreads like everything else
    u8* main_base = (u8*)linux_main_stack_base();

    if (main_base) {
        return main_base;
    }

#  endif

    pthread_attr_t attr;

    if (pthread_getattr_np(pthread_self(), &attr) != 0) {
        return NULL;
    }

    void*  base = NULL;
    size_t size = 0;

    if (pthread_attr_getstack(&attr, &base, &size) != 0 or size == 0) {
        base = NULL;
    }

    pthread_attr_destroy(&attr);

    return base;

#endif
}

bool m3_HostMapFile (const char* i_path, size_t i_maxBytes, M3HostFile* o_file)
{
    int fd = open(i_path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    struct stat info;

    // Only a regular file of a size worth having: a directory cannot be mapped, a
    // pipe or a device has no size to map, and an empty one is no module anyway.
    // Anything this turns down is read instead, which is what makes a module out of
    // a process substitution work.
    if (fstat(fd, &info) != 0 or not S_ISREG(info.st_mode) or info.st_size <= 0 or
        (uint64_t) info.st_size > (uint64_t)SIZE_MAX or
        (i_maxBytes and (uint64_t) info.st_size > (uint64_t)i_maxBytes)) {
        close(fd);
        return m3_HostReadFile(i_path, i_maxBytes, o_file);
    }

    size_t size = (size_t)info.st_size;
    void*  base = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (base == MAP_FAILED) {
        close(fd);
        return m3_HostReadFile(i_path, i_maxBytes, o_file);
    }

#if defined(LOCK_SH)
    // Shared, so that two wasm3 processes can run the same module, and
    // non-blocking, so that a file somebody else has locked is mapped anyway rather
    // than waiting on them. Failing changes nothing: the lock is advisory either
    // way, and a filesystem that has no opinion about locks is not a reason to
    // refuse the module. See m3_host.h for how little this promises.
    (void)flock(fd, LOCK_SH | LOCK_NB);
#endif

    // Unlike the mapping, the lock lives on the open file description, so the
    // descriptor has to stay open for as long as the mapping does
    o_file->data = base;
    o_file->size = size;
    o_file->handle = (intptr_t)fd;
    o_file->mapped = true;
    return true;
}

void m3_HostUnmapFile (M3HostFile* io_file)
{
    if (io_file->mapped) {
        munmap(io_file->data, io_file->size);

        if (io_file->handle != d_m3HostNoHandle) {
            close((int)io_file->handle);     // and with it the lock
        }
    } else {
        m3_Free(io_file->data);
    }

    io_file->data = NULL;
    io_file->size = 0;
    io_file->handle = d_m3HostNoHandle;
    io_file->mapped = false;
}

#if d_m3GuardedMemory

#  include <setjmp.h>
#  include <signal.h>

size_t m3_HostPageSize (void)
{
    long size = sysconf(_SC_PAGESIZE);

    return (size > 0) ? (size_t)size : 4096;
}

void* m3_HostReserve (size_t i_bytes)
{
    // MAP_NORESERVE so that a reservation this large is not charged against the
    // overcommit budget: almost none of it is ever going to be touched
    void* base = mmap(NULL, i_bytes, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

    return (base == MAP_FAILED) ? NULL : base;
}

bool m3_HostCommit (void* i_address, size_t i_bytes)
{
    if (i_bytes == 0) {
        return true;
    }

    // The pages are already there and already zero - the reservation was anonymous -
    // so committing is only a matter of being allowed to reach them
    return mprotect(i_address, i_bytes, PROT_READ | PROT_WRITE) == 0;
}

bool m3_HostDecommit (void* i_address, size_t i_bytes)
{
    if (i_bytes == 0) {
        return true;
    }

    // Mapping fresh anonymous pages over the range is what drops the old ones -
    // mprotect alone would leave them allocated and still holding what was written
    void* again = mmap(i_address, i_bytes, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0);

    return again == i_address;
}

void m3_HostRelease (void* i_address, size_t i_bytes)
{
    munmap(i_address, i_bytes);
}


// What a protected call left behind for the signal handler to find. Per-thread,
// because the fault arrives on the thread that caused it and nowhere else, and a
// chain, because a host function may call back into Wasm from inside one.
typedef struct M3GuardFrame {
    sigjmp_buf           jmp;
    const u8*            low;
    size_t               bytes;
    struct M3GuardFrame* previous;
} M3GuardFrame;

static M3_THREAD_LOCAL M3GuardFrame* g_guardFrame;

// The handlers that were there first. A fault this does not claim has to end the way
// it would have without any of this, so those are called rather than replaced.
static struct sigaction              g_previousSegv;
static struct sigaction              g_previousBus;
static bool                          g_handlersInstalled;

static
void guard_signal (int i_signal, siginfo_t* i_info, void* i_ucontext)
{
    const M3GuardFrame* frame = g_guardFrame;

    if (frame and i_info) {
        const u8* address = (const u8*)i_info->si_addr;

        if (address >= frame->low and address < frame->low + frame->bytes) {
            // Everything the interpreter was in the middle of is data on the heap,
            // so there is nothing here to unwind - see m3_HostProtectedCall
            siglongjmp(((M3GuardFrame*)frame)->jmp, 1);
        }
    }

    const struct sigaction* previous = (i_signal == SIGBUS) ? &g_previousBus : &g_previousSegv;

    if ((previous->sa_flags & SA_SIGINFO) and previous->sa_sigaction) {
        previous->sa_sigaction(i_signal, i_info, i_ucontext);
    } else if (previous->sa_handler != SIG_DFL and previous->sa_handler != SIG_IGN) {
        previous->sa_handler(i_signal);
    } else {
        // Put the default back and return: the instruction runs again, faults again,
        // and the process dies where it stood rather than somewhere in here
        struct sigaction dfl;
        memset(&dfl, 0, sizeof(dfl));
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        sigaction(i_signal, &dfl, NULL);
    }
}

static
void install_guard_handlers (void)
{
    if (g_handlersInstalled) {
        return;
    }
    g_handlersInstalled = true;

    struct sigaction action;
    memset(&action, 0, sizeof(action));

    action.sa_sigaction = guard_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&action.sa_mask);

    sigaction(SIGSEGV, &action, &g_previousSegv);

    // Darwin reports a write to a PROT_NONE page as SIGBUS where Linux reports
    // SIGSEGV, so both have to be claimed
    sigaction(SIGBUS, &action, &g_previousBus);
}

// A stack for the handler to run on, so that a fault which happens because the
// thread's own stack ran out still has somewhere to be handled. One per thread, and
// only where the thread has not already been given one.
static
void install_alt_stack (void)
{
    // A fixed size rather than SIGSTKSZ, which recent glibc made a call to sysconf
    // and so no longer something an array can be declared with. 64KiB is well over
    // what any of these handlers need - the claimed path is a siglongjmp, and the
    // unclaimed one is a call to whatever was there before.
    static M3_THREAD_LOCAL u8   stack[64 * 1024];
    static M3_THREAD_LOCAL bool installed;

    if (installed) {
        return;
    }
    installed = true;

    stack_t current;
    if (sigaltstack(NULL, &current) == 0 and not (current.ss_flags & SS_DISABLE)) {
        return;                          // this thread already has one
    }

    stack_t alt;
    alt.ss_sp = stack;
    alt.ss_size = sizeof(stack);
    alt.ss_flags = 0;

    sigaltstack(&alt, NULL);
}

bool m3_HostProtectedCall (void (*i_body)(void*), void* i_context,
                           void* i_guardLow, size_t i_guardBytes)
{
    install_guard_handlers();
    install_alt_stack();

    M3GuardFrame frame;
    frame.low = (const u8*)i_guardLow;
    frame.bytes = i_guardBytes;
    frame.previous = g_guardFrame;

    g_guardFrame = &frame;

    bool finished;

    // The mask is deliberately not saved: doing so costs a system call on every
    // call into Wasm, and the one thing it would have restored - SIGSEGV unblocked -
    // SA_NODEFER already arranges by never blocking it in the first place.
    if (sigsetjmp(frame.jmp, 0) == 0) {
        i_body(i_context);
        finished = true;
    } else {
        finished = false;
    }

    g_guardFrame = frame.previous;

    return finished;
}

#endif // d_m3GuardedMemory

#endif // m3_host_posix_h
