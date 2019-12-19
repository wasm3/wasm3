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

# ifndef d_m3CodePageAlignSize
#   define d_m3CodePageAlignSize                4096
# endif
# ifndef d_m3MaxFunctionStackHeight
#   define d_m3MaxFunctionStackHeight           2000
# endif

//# ifndef d_m3EnableFp32Maths
//#   define d_m3EnableFp32Maths                  false
//# endif
//# ifndef d_m3EnableFp64Maths
//#   define d_m3EnableFp64Maths                  true
//# endif

# ifndef d_m3LogOutput
#   define d_m3LogOutput                        1
# endif

# ifndef d_m3FixedHeap
#   define d_m3FixedHeap                        false
//# define d_m3FixedHeap                        (32*1024)
# endif

# ifndef d_m3VerboseErrorMessages
#   define d_m3VerboseErrorMessages           	1
# endif


// TODO: This flag is temporary
// It's enabled in build scripts for Linux, OS X, Win32 and Android targets
// and disabled on other platforms, i.e. microcontrollers
# ifndef d_m3AllocateLinearMemory
#   define d_m3AllocateLinearMemory             0
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
# define d_m3LogCompile         0
# define d_m3LogWasmStack       0
# define d_m3LogEmit            0
# define d_m3LogCodePages       0
# define d_m3LogModule          0
# define d_m3LogRuntime         0
# define d_m3LogExec            0
# define d_m3LogStackTrace      0
# define d_m3LogNativeStack     0


#endif /* m3_config_h */
