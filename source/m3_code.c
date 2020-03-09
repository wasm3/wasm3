//
//  m3_code.c
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_code.h"

IM3CodePage  NewCodePage  (u32 i_minNumLines)
{
    static u32 s_sequence = 0;

    IM3CodePage page;

    u32 pageSize = sizeof (M3CodePageHeader) + sizeof (code_t) * i_minNumLines;

    pageSize = (pageSize + (d_m3CodePageAlignSize-1)) & ~(d_m3CodePageAlignSize-1); // align
    m3Alloc ((void **) & page, u8, pageSize);

    if (page)
    {
        page->info.sequence = ++s_sequence;
        page->info.numLines = (pageSize - sizeof (M3CodePageHeader)) / sizeof (code_t);

        m3log (runtime, "new page: %p; seq: %d; bytes: %d; lines: %d", GetPagePC (page), page->info.sequence, pageSize, page->info.numLines);
    }

    return page;
}


void  FreeCodePages  (IM3CodePage * io_list)
{
    IM3CodePage page = * io_list;
    
    while (page)
    {
        m3log (code, "free page: %d; %p; util: %3.1f%%", page->info.sequence, page, 100. * page->info.lineIndex / page->info.numLines);

        IM3CodePage next = page->info.next;
        m3Free (page);
        page = next;
    }
    
    * io_list = NULL;
}


u32  NumFreeLines  (IM3CodePage i_page)
{
    d_m3Assert (i_page->info.lineIndex <= i_page->info.numLines);

    return i_page->info.numLines - i_page->info.lineIndex;
}


void  EmitWord_impl  (IM3CodePage i_page, void * i_word)
{                                                                       d_m3Assert (i_page->info.lineIndex+1 <= i_page->info.numLines);
    i_page->code [i_page->info.lineIndex++] = (void *) i_word;
}

void  EmitWord64_impl  (IM3CodePage i_page, const u64 i_word)
{                                                                       d_m3Assert (i_page->info.lineIndex+2 <= i_page->info.numLines);
    *((u64 *) & i_page->code [i_page->info.lineIndex]) = i_word;
    i_page->info.lineIndex += 2;
}


pc_t  GetPageStartPC  (IM3CodePage i_page)
{
    return & i_page->code [0];
}


pc_t  GetPagePC  (IM3CodePage i_page)
{
    if (i_page)
        return & i_page->code [i_page->info.lineIndex];
    else
        return NULL;
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



u32  FindCodePageEnd  (IM3CodePage i_list, IM3CodePage * o_end)
{
    u32 numPages = 0;
    * o_end = NULL;
    
    while (i_list)
    {
        * o_end = i_list;
        ++numPages;
        i_list = i_list->info.next;
    }
    
    return numPages;
}


u32  CountCodePages  (IM3CodePage i_list)
{
    IM3CodePage unused;
    return FindCodePageEnd (i_list, & unused);
}


IM3CodePage GetEndCodePage  (IM3CodePage i_list)
{
    IM3CodePage end;
    FindCodePageEnd (i_list, & end);
    
    return end;
}
