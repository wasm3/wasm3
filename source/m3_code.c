//
//  m3_code.c
//  M3: Massey Meta Machine
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_code.h"

#include <assert.h>

IM3CodePage  NewCodePage  (u32 i_minNumLines)
{
	static u32 s_sequence = 0;
	
	u32 pageSize = sizeof (M3CodePageHeader) + sizeof (code_t) * i_minNumLines;

	pageSize = (pageSize + 4095) & ~4095; // align to 4kB
	
	IM3CodePage page;
	m3Malloc ((void **) & page, pageSize);
	
	if (page)
	{
		page->info.sequence = ++s_sequence;						m3log (code, "new page: %d  size: %d", page->info.sequence, pageSize);
		
		page->info.numLines = (pageSize - sizeof (M3CodePageHeader)) / sizeof (code_t);;
	}
	
	return page;
}


void  FreeCodePages  (IM3CodePage i_page)
{
	while (i_page)
	{
		m3log (code, "free page: %d  util: %3.1f%%", i_page->info.sequence, 100. * i_page->info.lineIndex / i_page->info.numLines);
		
		IM3CodePage next = i_page->info.next;
		free (i_page);
		i_page = next;
	}
}


//void  CloseCodePage  (IM3CodePage i_page)
//{
//	i_page->info.slotIndex = c_m3CodePageNumSlots;	// force closed
//}


u32  NumFreeLines  (IM3CodePage i_page)
{
	TestCodePageCapacity (i_page);
	
	return i_page->info.numLines - i_page->info.lineIndex;
}


void  EmitWord  (IM3CodePage i_page, const void * const i_word)
{
	i_page->code [i_page->info.lineIndex++] = (void *) i_word;
	
	TestCodePageCapacity (i_page);

}


pc_t  GetPagePC  (IM3CodePage i_page)
{
	if (i_page)
		return & i_page->code [i_page->info.lineIndex];
	else
		return 0;
}


void  PushCodePage  (IM3CodePage * i_list, IM3CodePage i_codePage)
{
	IM3CodePage next = * i_list;
	i_codePage->info.next = next;
	* i_list = i_codePage;
}


IM3CodePage  PopCodePage  (IM3CodePage * i_list)
{
	IM3CodePage page = * i_list;
	* i_list = page->info.next;
	page->info.next = NULL;
	
	return page;
}

void  TestCodePageCapacity	(IM3CodePage i_page)
{
	#if DEBUG
		assert (i_page->info.lineIndex <= i_page->info.numLines);
	#endif
}
	
