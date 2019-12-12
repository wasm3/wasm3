//
//  m3_config.h
//  m3
//
//  Created by Steven Massey on 5/4/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_config_h
#define m3_config_h

#include "m3_config_platforms.h"

# ifndef d_m3MaxNumFunctionArgs
#   define d_m3MaxNumFunctionArgs               16
# endif
# ifndef d_m3CodePageSize
#   define d_m3CodePageSize                     4096
# endif
# ifndef d_m3AlignWasmMemoryToPages
#   define d_m3AlignWasmMemoryToPages           false
# endif
# ifndef d_m3MaxFunctionStackHeight
#   define d_m3MaxFunctionStackHeight           2000
# endif
# ifndef d_m3EnableOptimizations
#   define d_m3EnableOptimizations              false
# endif
# ifndef d_m3EnableFp32Maths
#   define d_m3EnableFp32Maths                  false
# endif
# ifndef d_m3EnableFp64Maths
#   define d_m3EnableFp64Maths                  true
# endif

# ifndef d_m3LogOutput
#   define d_m3LogOutput                        1
# endif

# ifndef d_m3FixedHeap
#   define d_m3FixedHeap                        false
//# define d_m3FixedHeap                        (32*1024)
# endif

// TODO: This flag is temporary
// It's enabled by default for Linux, OS X, Win32 and Android builds
// and disabled on other platforms, i.e. microcontrollers
# ifndef d_m3AllocateLinearMemory
#   define d_m3AllocateLinearMemory             true
# endif

# ifndef d_m3FixedHeapAlign
#   define d_m3FixedHeapAlign                   16
# endif

// logging ---------------------------------------------------------------------------

# define d_m3EnableOpProfiling      0
# define d_m3RuntimeStackDumps      0

# define d_m3TraceExec              (1 && d_m3RuntimeStackDumps && DEBUG)


// m3log (...) --------------------------------------------------------------------

# define d_m3LogParse           0
# define d_m3LogCompile         1
# define d_m3LogStack           1
# define d_m3LogEmit            1
# define d_m3LogCodePages       1
# define d_m3LogModule          0
# define d_m3LogRuntime         0
# define d_m3LogExec            1
# define d_m3LogNativeStack     0


#endif /* m3_config_h */
