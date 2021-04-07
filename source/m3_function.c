//
//  m3_function.c
//
//  Created by Steven Massey on 4/7/21.
//  Copyright © 2021 Steven Massey. All rights reserved.
//

#include "m3_function.h"



M3Result AllocFuncType (IM3FuncType * o_functionType, u32 i_numTypes)
{
	*o_functionType = (IM3FuncType) m3_Malloc (sizeof (M3FuncType) + i_numTypes);
	return (*o_functionType) ? m3Err_none : m3Err_mallocFailed;
}


bool  AreFuncTypesEqual  (const IM3FuncType i_typeA, const IM3FuncType i_typeB)
{
	if (i_typeA->numRets == i_typeB->numRets && i_typeA->numArgs == i_typeB->numArgs)
	{
		return (memcmp (i_typeA->types, i_typeB->types, i_typeA->numRets + i_typeA->numArgs) == 0);
	}

	return false;
}

u32  GetFuncTypeNumReturns  (const IM3FuncType i_funcType)
{
	return i_funcType ? i_funcType->numRets : 0;
}

