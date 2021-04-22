//
//  m3_emit.c
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
    M3Result result = m3Err_none;

    i_numLines += 2; // room for Bridge

    if (NumFreeLines (o->page) < i_numLines)
    {
        IM3CodePage page = AcquireCodePageWithCapacity (o->runtime, i_numLines);

        if (page)
        {
            m3log (emit, "bridging new code page from: %d %p (free slots: %d) to: %d", o->page->info.sequence, GetPC (o), NumFreeLines (o->page), page->info.sequence);
            d_m3Assert (NumFreeLines (o->page) >= 2);

            EmitWord (o->page, op_Branch);
            EmitWord (o->page, GetPagePC (page));

            ReleaseCodePage (o->runtime, o->page);

            o->page = page;
        }
        else result = m3Err_mallocFailedCodePage;
    }

    return result;
}


// have execution jump to a new page if slots are critically low
M3Result  BridgeToNewPageIfNecessary  (IM3Compilation o)
{
    return EnsureCodePageNumLines (o, d_m3CodePageFreeLinesThreshold);
}


M3Result  EmitOp  (IM3Compilation o, IM3Operation i_operation)
{
    M3Result result = m3Err_none;                                 d_m3Assert (i_operation or IsStackPolymorphic (o));

    // it's OK for page to be null; when compile-walking the bytecode without emitting
    if (o->page)
    {
# if d_m3EnableOpTracing
        if (i_operation != op_DumpStack)
            o->numEmits++;
# endif

        result = BridgeToNewPageIfNecessary (o);

        if (not result)
        {                                                           if (d_m3LogEmit) log_emit (o, i_operation);
# if d_m3RecordBacktraces
            EmitMappingEntry (o->page, o->lastOpcodeStart - o->module->wasmStart);
# endif // d_m3RecordBacktraces
            EmitWord (o->page, i_operation);
        }
    }

    return result;
}


// Push an immediate constant into the M3 codestream
void  EmitConstant32  (IM3Compilation o, const u32 i_immediate)
{
    if (o->page)
        EmitWord32 (o->page, i_immediate);
}

void  EmitSlotOffset  (IM3Compilation o, const i32 i_offset)
{
    if (o->page)
        EmitWord32 (o->page, i_offset);
}


pc_t  EmitPointer  (IM3Compilation o, const void * const i_pointer)
{
    pc_t ptr = GetPagePC (o->page);

    if (o->page)
        EmitWord (o->page, i_pointer);

    return ptr;
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
