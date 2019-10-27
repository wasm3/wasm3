//
//  Created by Steven Massey on 4/15/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef M3UTIL_H
#define M3UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "m3.h"
#include "m3_config.h"

typedef double			f64;
typedef float			f32;
typedef uint64_t		u64;
typedef int64_t			i64;
typedef uint32_t		u32;
typedef int32_t 		i32;
typedef uint16_t		u16;
typedef int16_t 		i16;
typedef uint8_t			u8;
typedef int8_t			i8;


typedef const void *			m3ret_t;
typedef const char * 			cstr_t;
typedef const char * const 		ccstr_t;
typedef const u8 * 				bytes_t;
typedef const u8 * const 		cbytes_t;

typedef i64						m3reg_t;
typedef u64 *					m3stack_t;

typedef
const void * const	cvptr_t;

# if !defined(__cplusplus) || defined(M3_COMPILER_MSVC)
# 	define not 		!
# 	define and 		&&
# 	define or		||
# endif

#define M3_COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

# ifdef DEBUG
#	define M3_FILE __FILE__
#	define M3_LINE __LINE__
#else
#	define M3_FILE ""
#	define M3_LINE 0
#endif

static const char * m3LogTruncFilename (const char * i_file)
{
	const char * file = i_file + strlen (i_file);

	while (true)
	{
		char c = * (file - 1);
		if (c == '/' or c == '\\')
			break;

		--file;
	}

	return file;
}



# define d_m3Log_parse d_m3LogParse			// required for m3logif
# define d_m3Log_stack d_m3LogStack
# define d_m3Log_runtime d_m3LogRuntime
# define d_m3Log_exec d_m3LogExec

# if d_m3LogOutput

// with filename:
//#	define d_m3Log(CATEGORY, FMT, ...) 					printf (" %-12s | %-8s |  " FMT, m3LogTruncFilename (M3_FILE), #CATEGORY, ##__VA_ARGS__);
#	define d_m3Log(CATEGORY, FMT, ...) 					printf (" %8s  |  " FMT, #CATEGORY, ##__VA_ARGS__);

#	if d_m3LogParse
#		define m3log_parse(CATEGORY, FMT, ...) 			d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#	else
#		define m3log_parse(...) {}
#	endif

#	if d_m3LogCompile
#		define m3log_compile(CATEGORY, FMT, ...) 		d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#	else
#		define m3log_compile(...) {}
#	endif

#	if d_m3LogStack
#		define m3log_stack(CATEGORY, FMT, ...) 			d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#	else
#		define m3log_stack(...) {}
#	endif

#	if d_m3LogEmit
#		define m3log_emit(CATEGORY, FMT, ...) 			d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#	else
#		define m3log_emit(...) {}
#	endif

#	if d_m3LogCodePages
#		define m3log_code(CATEGORY, FMT, ...) 			d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#	else
#		define m3log_code(...) {}
#	endif

#	if d_m3LogModule
#		define m3log_module(CATEGORY, FMT, ...) 		d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#	else
#		define m3log_module(...) {}
#	endif

#	if d_m3LogRuntime
#		define m3log_runtime(CATEGORY, FMT, ...) 		d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#	else
#		define m3log_runtime(...) {}
#	endif

#	if d_m3LogExec
#		define m3log_exec(CATEGORY, FMT, ...) 			d_m3Log(CATEGORY, FMT, ##__VA_ARGS__)
#	else
#		define m3log_exec(...) {}
#	endif

#	define m3log(CATEGORY, FMT, ...)					m3log_##CATEGORY (CATEGORY, FMT "\n", ##__VA_ARGS__)
#	define m3logif(CATEGORY, STATEMENT)					m3log_##CATEGORY (CATEGORY, ""); if (d_m3Log_##CATEGORY) { STATEMENT; printf ("\n"); }
# else
#	define m3log(CATEGORY, FMT, ...)					{}
#	define m3logif(CATEGORY, STATEMENT)					{}
# endif


# if DEBUG
#	define d_m3Assert(ASS)		assert (ASS)
#	define d_m3AssertFatal(ASS)	assert (ASS)
# else
#	define d_m3Assert(ASS)
#	define d_m3AssertFatal(ASS)	m3AbortIfNot (ASS)
# endif

typedef void /*const*/ *					code_t;
typedef code_t const * /*__restrict__*/		pc_t;


typedef struct M3MemoryHeader
{
	IM3Module		module;
	void *			end;
}
M3MemoryHeader;


typedef struct M3CodePageInfo
{
	struct M3CodePage *		next;

	u32						lineIndex;
	u32						numLines;
	u32						sequence;		// this is just used for debugging; could be removed
	u32						__;
}
M3CodePageHeader;


#define	c_m3CodePageFreeLinesThreshold		10

#define	c_m3MemPageSize						65536
#define	c_m3MaxFunctionStackHeight			d_m3MaxFunctionStackHeight
#define	c_m3MaxFunctionLocals				512

#define	c_m3Reg0Id							c_m3MaxFunctionStackHeight + 1
#define	c_m3Fp0Id							c_m3MaxFunctionStackHeight + 2

#define	c_m3MaxNumFunctionConstants 		60

#define	c_m3AlignWasmMemoryToPages			d_m3AlignWasmMemoryToPages

//#define	c_m3MaxSaneWasmSize				1000000000
#define	c_m3MaxSaneUtf8Length 				2000
#define	c_m3MaxNumFunctionArgs				d_m3MaxNumFunctionArgs

#define c_externalKind_function 			0
#define c_externalKind_table 		 		1
#define c_externalKind_memory				2
#define c_externalKind_global				3

static const char * const c_waTypes [] 				= { "nil", "i32", "i64", "f32", "f64", "void", "void *" };


#define m3Alloc(OPTR, STRUCT, NUM) m3Malloc ((void **) OPTR, sizeof (STRUCT) * (NUM))
#define m3RellocArray(PTR, STRUCT, NEW, OLD) m3Realloc ((PTR), sizeof (STRUCT) * (NEW), sizeof (STRUCT) * (OLD))

#define _m3Error(RESULT, RT, MOD, FUN, FILE, LINE, FORMAT, ...)	m3Error (RESULT, RT, MOD, FUN, FILE, LINE, FORMAT, ##__VA_ARGS__)

#define ErrorModule(RESULT, MOD, FORMAT, ...)	_m3Error (RESULT, MOD->runtime, MOD, NULL,	M3_FILE, M3_LINE, FORMAT, ##__VA_ARGS__)
#define ErrorCompile(RESULT, COMP, FORMAT, ...)	_m3Error (RESULT, COMP->runtime, COMP->module, NULL, M3_FILE, M3_LINE, FORMAT, ##__VA_ARGS__)
//#define ErrorExec(RESULT, MODULE, FORMAT, ...)	_m3Error (RESULT, COMP->runtime, COMP->module, NULL, M3_FILE, M3_LINE, FORMAT, ##__VA_ARGS__)

#ifndef min
#define min(A,B) (A < B) ? A : B
#endif
#ifndef max
#define max(A,B) (A > B) ? A : B
#endif

void		m3NotImplemented		();
void		m3AbortIfNot			(bool condition);

void		m3Yield					();

M3Result	m3Malloc				(void ** o_ptr, size_t i_size);
void * 		m3Realloc				(void * i_ptr, size_t i_newSize, size_t i_oldSize);
void		m3Free_impl				(void * o_ptr);

#define		m3Free(P)				{ m3Free_impl((void*)(P)); P = NULL; }

bool		IsIntType				(u8 i_wasmType);
bool		IsFpType				(u8 i_wasmType);
M3Result	NormalizeType			(u8 * o_type, i8 i_convolutedWasmType);
u32 		SizeOfType				(u8 i_m3Type);

M3Result	Read_u64				(u64 * o_value, const u8 ** io_bytes, cbytes_t i_end);
M3Result 	Read_u32				(u32 * o_value, const u8 ** io_bytes, cbytes_t i_end);
M3Result	Read_u8					(u8 * o_value, const u8 ** io_bytes, cbytes_t i_end);

M3Result	ReadLebUnsigned			(u64 * o_value, i32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end);
M3Result	ReadLebSigned			(i64 * o_value, i32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end);
M3Result	ReadLEB_u32				(u32 * o_value, bytes_t* io_bytes, cbytes_t i_end);
M3Result	ReadLEB_u7				(u8 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result	ReadLEB_i7				(i8 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result	ReadLEB_i32				(i32 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result	ReadLEB_i64				(i64 * o_value, bytes_t * io_bytes, cbytes_t i_end);
M3Result	Read_utf8				(cstr_t * o_utf8, bytes_t * io_bytes, cbytes_t i_end);

size_t		SPrintArg				(char * o_string, size_t i_n, m3stack_t i_sp, u8 i_type);

void		ReportError				(IM3Runtime io_runtime, IM3Module i_module, IM3Function i_function, ccstr_t i_errorMessage, ccstr_t i_file, u32 i_lineNum);


//static __inline__ unsigned long GetCC(void)
//{
//	unsigned a, d;
//	asm volatile("rdtsc" : "=a" (a), "=d" (d));
//	return ((unsigned long)a) | (((unsigned long)d) << 32);
//}

#endif
