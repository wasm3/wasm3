//
//  m3_bind.h
//  m3
//
//  Created by Steven Massey on 2/27/20.
//  Copyright © 2020 Steven Massey. All rights reserved.
//

#ifndef m3_bind_h
#define m3_bind_h

#include "m3_core.h"

u8          ConvertTypeCharToTypeId     (char i_code);
M3Result    SignatureToFuncType         (M3FuncType * o_functionType, ccstr_t i_signature);


#endif /* m3_bind_h */
