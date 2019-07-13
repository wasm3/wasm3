//
//  m3_optimize.c
//  m3
//
//  Created by Steven Massey on 4/27/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_compile.h"
#include "m3_exec.h"


// not currently used now.
bool  PeekNextOpcode  (IM3Compilation o, u8 i_opcode)
{
	bool found = false;
	
	if (o->wasm < o->wasmEnd)
	{
		u8 opcode = * o->wasm;
		
		if (opcode == i_opcode)
		{
			found = true;
			o->wasm++;
		}
	}
	
	return found;
}


