//
//  m3_function.h
//
//  Created by Steven Massey on 4/7/21.
//  Copyright © 2021 Steven Massey. All rights reserved.
//

#ifndef m3_function_h
#define m3_function_h

#include "m3_core.h"

d_m3BeginExternC


typedef struct M3FuncType
{
	struct M3FuncType *     next;

	u32                     numRets;
	u32                     numArgs;
	u8                      types [];        // returns, then args
}
M3FuncType;

typedef M3FuncType *        IM3FuncType;


M3Result    AllocFuncType                   (IM3FuncType * o_functionType, u32 i_numTypes);
bool        AreFuncTypesEqual               (const IM3FuncType i_typeA, const IM3FuncType i_typeB);
u32			GetFuncTypeNumReturns			(const IM3FuncType i_funcType);

d_m3EndExternC

#endif /* m3_function_h */
