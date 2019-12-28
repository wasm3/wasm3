//
//  m3_emit.c
//  m3
//
//  Created by Steven Massey on 7/9/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_emit.h"
#include "m3_info.h"
#include "m3_exec.h"

M3Result  EnsureCodePageNumLines  (IM3Compilation o, u32 i_numLines)
{
    M3Result result = c_m3Err_none;

    i_numLines += 2; // room for Bridge

    if (NumFreeLines (o->page) < i_numLines)
    {
        IM3CodePage page = AcquireCodePageWithCapacity (o->runtime, i_numLines);

        if (page)
        {
            d_m3Assert (NumFreeLines (o->page) >= 2);
            m3log (emit, "bridging new code page from: %d %p (free slots: %d) to: %d", o->page->info.sequence, GetPC (o), NumFreeLines (o->page), page->info.sequence);
            
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
    return EnsureCodePageNumLines (o, c_m3CodePageFreeLinesThreshold);
}


M3Result  EmitOp  (IM3Compilation o, IM3Operation i_operation)
{
    M3Result result = c_m3Err_none;                                 d_m3Assert (i_operation or IsStackPolymorphic(o));

    // it's OK for page to be null; when compile-walking the bytecode without emitting
    if (o->page)
    {
#   if d_m3RuntimeStackDumps
        if (i_operation != op_DumpStack)
#   endif
            o->numEmits++;

        result = BridgeToNewPageIfNecessary (o);

        if (not result)
        {                                                           m3logif (emit, log_emit (o, i_operation))
            EmitWord (o->page, i_operation);
        }
    }

    return result;
}


// this pushes an immediate constant into the M3 codestream
void  EmitConstant  (IM3Compilation o, const u64 i_immediate)
{
    if (o->page)
        EmitWord (o->page, i_immediate);
}

void  EmitConstant64  (IM3Compilation o, const u64 i_const)
{
    if (o->page)
        EmitWord64 (o->page, i_const);
}


void  EmitOffset  (IM3Compilation o, const i32 i_offset)
{
    if (o->page)
        EmitWord (o->page, i_offset);
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




