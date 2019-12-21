//
//  m3_api_defs.h
//
//  Created by Volodymyr Shymanskyy on 12/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#ifndef m3_api_defs_h
#define m3_api_defs_h

#include "m3_core.h"

#define m3ApiGetReturn(TYPE, NAME) TYPE* NAME = ((TYPE *) (_sp));
#define m3ApiGetArg(TYPE, NAME)    TYPE NAME = * ((TYPE *) (_sp++));
#define m3ApiGetArgPtr(TYPE, NAME) TYPE NAME = (TYPE) (_mem + * (u32 *) _sp++);

#define m3ApiRawFunction(NAME)     void NAME (u64 * _sp, u8 * _mem)

#endif /* m3_api_defs_h */
