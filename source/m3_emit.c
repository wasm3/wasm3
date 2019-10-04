//
//  m3_emit.c
//  m3
//
//  Created by Steven Massey on 7/9/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_emit.h"


M3Result  EnsureCodePageNumLines  (IM3Compilation o, u32 i_numLines)
{
	M3Result result = c_m3Err_none;
	
	i_numLines += 2; // room for Bridge
	
	if (NumFreeLines (o->page) < i_numLines)
	{
		IM3CodePage page = AcquireCodePageWithCapacity (o->runtime, i_numLines);
		
		if (page)
		{
			m3log (code, "bridging new code page from: %d (free slots: %d)", o->page->info.sequence, NumFreeLines (o->page));
			
			EmitWord (o->page, op_Bridge);
			EmitWord (o->page, GetPagePC (page));
			
			ReleaseCodePage (o->runtime, o->page);
			
			o->page = page;
		}
		else result = c_m3Err_mallocFailedCodePage;
	}
	
	return result;
}


// have execution jump to a new page if slots are critically low
M3Result  BridgeToNewPageIfNecessary  (IM3Compilation o)
{
	return EnsureCodePageNumLines (o, c_m3CodePageFreeLinesThreshold - 2);
}


M3Result  EmitOp  (IM3Compilation o, IM3Operation i_operation)
{
	M3Result result = c_m3Err_none;
	
	// it's OK for page to be null; when compile-walking the bytecode without emitting
	if (o->page)
	{
#if d_m3RuntimeStackDumps
		if (i_operation != op_DumpStack)
#endif
			o->numEmits++;
		
		result = BridgeToNewPageIfNecessary (o);
		
		if (not result)
			EmitWord (o->page, i_operation);
	}
	
	return result;
}


// this pushes an immediate constant into the M3 codestream
void  EmitConstant  (IM3Compilation o, const u64 i_immediate)
{
	if (o->page)
		EmitWord (o->page, (const void *) i_immediate);
}


void  EmitOffset  (IM3Compilation o, const i32 i_offset)
{
	if (o->page)
		EmitWord (o->page, (const void *) (i64) i_offset);
}


void  EmitPointer  (IM3Compilation o, const void * const i_pointer)
{
	if (o->page)
		EmitWord (o->page, i_pointer);
}

void * ReservePointer (IM3Compilation o)
{
	pc_t ptr = GetPagePC (o->page);
	EmitPointer (o, NULL);
	return (void *) ptr;
}


pc_t GetPC (IM3Compilation o)
{
	return GetPagePC (o->page);
}




