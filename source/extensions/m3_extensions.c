//
//  m3_extensions.c
//
//  Created by Steven Massey on 3/30/21.
//  Copyright © 2021 Steven Massey. All rights reserved.
//

#include "wasm3_ext.h"
#include "m3_env.h"


IM3Module  m3_NewModule  (IM3Environment i_environment)
{
	IM3Module module = m3_AllocStruct (M3Module);
	
	if (module)
	{
		module->name = ".unnamed";
		module->startFunction = -1;
		module->environment = i_environment;

		module->wasmStart = NULL;
		module->wasmEnd = NULL;
	}
	
	return module;
}
