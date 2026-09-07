//
//  m3_host_none.h
//
//  The implementation of m3_host.h for a system with neither a POSIX nor a Win32
//  face: a bare-metal target, an RTOS, or anything else. Most of the ports under
//  platforms/ land here.
//
//  There is no address space to reserve and no stack to measure, and "cannot say" is
//  a supported answer to both, so a build gets those for nothing. A file is the one
//  thing it still does, by reading it - which is the whole of what such a system can
//  do, and costs it stdio.
//
//  A header rather than a translation unit of its own, and included by m3_core.c
//  alone: a new .c would have to be added to every source list that names them one by
//  one, and most of those are in build scripts outside this repository.
//

#ifndef m3_host_none_h
#define m3_host_none_h

#include "m3_core.h"
#include "m3_host.h"

void* m3_HostStackBase (void)
{
    return NULL;
}

// No mmap here, so the module's bytes are read directly onto the heap
bool m3_HostMapFile (const char* i_path, size_t i_maxBytes, M3HostFile* o_file)
{
    return m3_HostReadFile(i_path, i_maxBytes, o_file);
}

void m3_HostUnmapFile (M3HostFile* io_file)
{
    m3_Free(io_file->data);

    io_file->size = 0;
    io_file->handle = d_m3HostNoHandle;
    io_file->mapped = false;
}

#if d_m3GuardedMemory
#  error "d_m3GuardedMemory needs a system that can reserve address space; this one has none"
#endif

#endif // m3_host_none_h
