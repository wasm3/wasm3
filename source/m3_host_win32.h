//
//  m3_host_win32.h
//
//  The Win32 implementation of m3_host.h.
//
//  A header rather than a translation unit of its own, and included by m3_core.c
//  alone: a new .c would have to be added to every source list that names them one by
//  one, and most of those are in build scripts outside this repository.
//

#ifndef m3_host_win32_h
#define m3_host_win32_h

#include "m3_core.h"

#if !defined(_WIN32)
#  error "This file is for Windows only"
#endif

#include "m3_host.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// A thread's stack is one reservation, and any address inside it answers with the
// base of the whole thing - guard page included. That is what makes VirtualQuery
// enough here: no version check, no other library, and it describes a fibre's stack
// as readily as a thread's. GetCurrentThreadStackLimits would say the same, and only
// from Windows 8 on.
void* m3_HostStackBase (void)
{
    MEMORY_BASIC_INFORMATION mbi;

    // any address on the stack being asked about will do, and this one is on it
    volatile char            here = 0;

    if (VirtualQuery((LPCVOID)&here, &mbi, sizeof(mbi)) == 0) {
        return NULL;
    }

    return mbi.AllocationBase;
}

// FILE_SHARE_READ and nothing else: another process may read the file, and one
// trying to write or delete it is refused for as long as the mapping lives. The
// kernel enforces that, so it is the real thing rather than the advisory lock POSIX
// has to make do with - see m3_host.h.
bool m3_HostMapFile (const char* i_path, size_t i_maxBytes, M3HostFile* o_file)
{
    HANDLE file = CreateFileA(i_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size;

    // Anything with no size to map - a pipe, a console - is read instead, and so is
    // a file too large to address or larger than the caller wants
    if (not GetFileSizeEx(file, &size) or size.QuadPart <= 0 or
        (ULONGLONG) size.QuadPart > (ULONGLONG)SIZE_MAX or
        (i_maxBytes and (ULONGLONG) size.QuadPart > (ULONGLONG)i_maxBytes)) {
        CloseHandle(file);
        return m3_HostReadFile(i_path, i_maxBytes, o_file);
    }

    HANDLE mapping = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(file);                   // the mapping holds the file open

    if (mapping == NULL) {
        return m3_HostReadFile(i_path, i_maxBytes, o_file);
    }

    void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);                // and the view holds the mapping

    if (base == NULL) {
        return m3_HostReadFile(i_path, i_maxBytes, o_file);
    }

    // Nothing left to keep: the view holds the mapping, which holds the file, and
    // the share mode with it
    o_file->data = base;
    o_file->size = (size_t)size.QuadPart;
    o_file->handle = d_m3HostNoHandle;
    o_file->mapped = true;
    return true;
}

void m3_HostUnmapFile (M3HostFile* io_file)
{
    if (io_file->mapped) {
        UnmapViewOfFile(io_file->data);  // the view knows how big it is
    } else {
        m3_Free(io_file->data);
    }

    io_file->data = NULL;
    io_file->size = 0;
    io_file->handle = d_m3HostNoHandle;
    io_file->mapped = false;
}


#if d_m3GuardedMemory

size_t m3_HostPageSize (void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);

    // dwAllocationGranularity, not dwPageSize: a reservation starts on the coarser
    // of the two, so that is the grain the whole scheme has to be cut in
    return info.dwAllocationGranularity ? info.dwAllocationGranularity : 65536;
}

void* m3_HostReserve (size_t i_bytes)
{
    return VirtualAlloc(NULL, i_bytes, MEM_RESERVE, PAGE_NOACCESS);
}

bool m3_HostCommit (void* i_address, size_t i_bytes)
{
    if (i_bytes == 0) {
        return true;
    }
    return VirtualAlloc(i_address, i_bytes, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

bool m3_HostDecommit (void* i_address, size_t i_bytes)
{
    if (i_bytes == 0) {
        return true;
    }
    return VirtualFree(i_address, i_bytes, MEM_DECOMMIT) != 0;
}

void m3_HostRelease (void* i_address, size_t i_bytes)
{
    (void)i_bytes;                       // MEM_RELEASE frees the whole reservation
    VirtualFree(i_address, 0, MEM_RELEASE);
}

// Which faults are ours, for the filter below. Read from the exception record
// rather than from anything of ours, so nothing here depends on what the faulting
// code was in the middle of.
static
bool guard_fault (EXCEPTION_POINTERS* i_info, void* i_low, size_t i_bytes)
{
    if (i_info == NULL or i_info->ExceptionRecord == NULL) {
        return false;
    }

    const EXCEPTION_RECORD* record = i_info->ExceptionRecord;

    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION or
        record->NumberParameters < 2) {
        return false;
    }

    // [0] says read or write, [1] is the address that could not be reached
    const u8* address = (const u8*)record->ExceptionInformation[1];

    return address >= (const u8*)i_low and address < (const u8*)i_low + i_bytes;
}

// Structured exception handling rather than a vectored handler: the unwind is the
// system's to do, so the stack between here and the fault is given back properly
// instead of being jumped over. It is also why this needs a compiler that speaks
// __try - see d_m3GuardedMemory.
bool m3_HostProtectedCall (void (*i_body)(void*), void* i_context,
                           void* i_guardLow, size_t i_guardBytes)
{
    __try {
        i_body(i_context);
        return true;
    } __except (guard_fault(GetExceptionInformation(), i_guardLow, i_guardBytes)
                  ? EXCEPTION_EXECUTE_HANDLER
                  : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

#endif // d_m3GuardedMemory

#endif // m3_host_win32_h
