//
//  m3_code.h
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_code_h
#define m3_code_h

#include "m3_core.h"


typedef struct M3CodePage
{
    M3CodePageHeader        info;
    code_t                  code                [1];
}
M3CodePage;

typedef M3CodePage *    IM3CodePage;


IM3CodePage             NewCodePage             (u32 i_minNumLines);

void                    FreeCodePages           (IM3CodePage i_page);
u32                     NumFreeLines            (IM3CodePage i_page);
pc_t                    GetPageStartPC          (IM3CodePage i_page);
pc_t                    GetPagePC               (IM3CodePage i_page);
void                    EmitWord64_impl         (IM3CodePage i_page, u64 i_word);
void                    EmitWord_impl           (IM3CodePage i_page, void* i_word);

void                    PushCodePage            (IM3CodePage * i_list, IM3CodePage i_codePage);
IM3CodePage             PopCodePage             (IM3CodePage * i_list);

# ifdef DEBUG
void                    dump_code_page            (IM3CodePage i_codePage, pc_t i_startPC);
# endif

#define EmitWord(page, val) EmitWord_impl(page, (void*)(val))

#if M3_SIZEOF_PTR == 4
#  define EmitWord64(page, val) EmitWord64_impl(page, (u64)(val))
#else
#  define EmitWord64(page, val) EmitWord_impl(page, (void*)(val))
#endif

#endif // m3_code_h
