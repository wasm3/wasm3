//
//  m3_exec.c
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_exec.h"
#include "m3_compile.h"


# if d_m3EnableOpProfiling
//--------------------------------------------------------------------------------------------------------
static M3ProfilerSlot s_opProfilerCounts [d_m3ProfilerSlotMask + 1] = {};

void  ProfileHit  (cstr_t i_operationName)
{
    u64 ptr = (u64) i_operationName;

    M3ProfilerSlot * slot = & s_opProfilerCounts [ptr & d_m3ProfilerSlotMask];

    if (slot->opName)
    {
        if (slot->opName != i_operationName)
        {
            m3_Abort ("profiler slot collision; increase d_m3ProfilerSlotMask");
        }
    }

    slot->opName = i_operationName;
    slot->hitCount++;
}


void  m3_PrintProfilerInfo  ()
{
    M3ProfilerSlot dummy;
    M3ProfilerSlot * maxSlot = & dummy;

    do
    {
        maxSlot->hitCount = 0;

        for (u32 i = 0; i <= d_m3ProfilerSlotMask; ++i)
        {
            M3ProfilerSlot * slot = & s_opProfilerCounts [i];

            if (slot->opName)
            {
                if (slot->hitCount > maxSlot->hitCount)
                    maxSlot = slot;
            }
        }

        if (maxSlot->opName)
        {
            fprintf (stderr, "%13llu  %s\n", maxSlot->hitCount, maxSlot->opName);
            maxSlot->opName = NULL;
        }
    }
    while (maxSlot->hitCount);
}

# else

void  m3_PrintProfilerInfo  () {}

# endif
