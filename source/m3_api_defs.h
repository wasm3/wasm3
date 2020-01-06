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

#define m3ApiRawFunction(NAME)     const void * NAME (IM3Runtime runtime, uint64_t * _sp, void * _mem)
#define m3ApiReturn(VALUE)         { *raw_return = (VALUE); return m3Err_none; }
#define m3ApiTrap(VALUE)           { return VALUE; }

#endif // m3_api_defs_h
