//
//  m3_api_defs.h
//
//  Created by Volodymyr Shymanskyy on 12/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#ifndef m3_api_defs_h
#define m3_api_defs_h

#include "m3_core.h"

// TODO: perform bounds checks
#define m3ApiOffsetToPtr(offset)   (void*)((u8*)_mem + (u32)(offset))
#define m3ApiPtrToOffset(ptr)      (u32)((u8*)ptr - (u8*)_mem)

#define m3ApiReturnType(TYPE)      TYPE* raw_return = ((TYPE*) (_sp));
#define m3ApiGetArg(TYPE, NAME)    TYPE NAME = * ((TYPE *) (_sp++));
#define m3ApiGetArgMem(TYPE, NAME) TYPE NAME = (TYPE)m3ApiOffsetToPtr(* ((u32 *) (_sp++)));

#if d_m3SkipMemoryBoundsCheck
# define m3ApiCheckMem(off, len)
#else
# define m3ApiCheckMem(off, len)   { if (UNLIKELY(off == _mem || ((u64)(off) + (len)) > ((u64)(_mem)+runtime->memory.mallocated->length))) m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess); }
#endif

#define m3ApiRawFunction(NAME)     const void * NAME (IM3Runtime runtime, IM3ImportContext _ctx, uint64_t * _sp, void * _mem)
#define m3ApiReturn(VALUE)         { *raw_return = (VALUE); return m3Err_none; }
#define m3ApiTrap(VALUE)           { return VALUE; }
#define m3ApiSuccess()             { return m3Err_none; }

# if defined(M3_BIG_ENDIAN)
#  define m3ApiReadMem16(ptr)        __builtin_bswap16((* (u16 *)(ptr)))
#  define m3ApiReadMem32(ptr)        __builtin_bswap32((* (u32 *)(ptr)))
#  define m3ApiReadMem64(ptr)        __builtin_bswap64((* (u64 *)(ptr)))
#  define m3ApiWriteMem16(ptr, val)  { * (u16 *)(ptr) = __builtin_bswap16((val)); }
#  define m3ApiWriteMem32(ptr, val)  { * (u32 *)(ptr) = __builtin_bswap32((val)); }
#  define m3ApiWriteMem64(ptr, val)  { * (u64 *)(ptr) = __builtin_bswap64((val)); }
# else
#  define m3ApiReadMem16(ptr)        (* (u16 *)(ptr))
#  define m3ApiReadMem32(ptr)        (* (u32 *)(ptr))
#  define m3ApiReadMem64(ptr)        (* (u64 *)(ptr))
#  define m3ApiWriteMem16(ptr, val)  { * (u16 *)(ptr) = (val); }
#  define m3ApiWriteMem32(ptr, val)  { * (u32 *)(ptr) = (val); }
#  define m3ApiWriteMem64(ptr, val)  { * (u64 *)(ptr) = (val); }
# endif

#endif // m3_api_defs_h
