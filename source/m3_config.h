//
//  m3_config.h
//  m3
//
//  Created by Steven Massey on 5/4/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_config_h
#define m3_config_h

//TODO: move to a separate file
# if defined(PARTICLE)
#	define d_m3LogOutput						false
#	define d_m3MaxFunctionStackHeight			256
# endif

# if defined(__clang__)
#  define M3_COMPILER_CLANG 1
# elif defined(__GNUC__) || defined(__GNUG__)
#  define M3_COMPILER_GCC 1
# elif defined(_MSC_VER)
#  define M3_COMPILER_MSVC 1
# else
#  warning "Compiler not detected"
# endif

# ifndef d_m3MaxNumFunctionArgs
#	define d_m3MaxNumFunctionArgs				16
# endif
# ifndef d_m3CodePageSize
#	define d_m3CodePageSize						4096
# endif
# ifndef d_m3AlignWasmMemoryToPages
#	define d_m3AlignWasmMemoryToPages			false
# endif
# ifndef d_m3MaxFunctionStackHeight
#	define d_m3MaxFunctionStackHeight			2000
# endif
# ifndef d_m3EnableOptimizations
#	define d_m3EnableOptimizations				false
# endif
# ifndef d_m3EnableFp32Maths
#	define d_m3EnableFp32Maths					false
# endif
# ifndef d_m3EnableFp64Maths
#	define d_m3EnableFp64Maths					true
# endif

# ifndef d_m3LogOutput
#	define d_m3LogOutput						true
# endif

# ifndef d_m3FixedHeap
#	define d_m3FixedHeap						false
//#	define d_m3FixedHeap						(32*1024)
# endif

# ifndef d_m3FixedHeapAlign
#	define d_m3FixedHeapAlign					16
# endif

// logging ---------------------------------------------------------------------------

# define d_m3EnableOpProfiling		0
# define d_m3RuntimeStackDumps		0

# define d_m3TraceExec 				(1 && d_m3RuntimeStackDumps && DEBUG)


// m3log (...) --------------------------------------------------------------------

# define d_m3LogParse			0
# define d_m3LogCompile			0
# define d_m3LogStack			0
# define d_m3LogEmit			0
# define d_m3LogCodePages		0
# define d_m3LogModule			0
# define d_m3LogRuntime			0
# define d_m3LogExec			0
# define d_m3LogNativeStack		0


#endif /* m3_config_h */
