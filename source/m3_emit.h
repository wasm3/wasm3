//
//  m3_emit.h
//
//  Created by Steven Massey on 7/9/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_emit_h
#define m3_emit_h

#include "m3_compile.h"

d_m3BeginExternC

M3Result    BridgeToNewPageIfNecessary  (IM3Compilation o);
M3Result    EnsureCodePageNumLines      (IM3Compilation o, u32 i_numLines);

M3Result    EmitOp                      (IM3Compilation o, IM3Operation i_operation);
void        EmitConstant32              (IM3Compilation o, const u32 i_immediate);
void        EmitSlotOffset              (IM3Compilation o, const i32 i_offset);
pc_t        EmitPointer                 (IM3Compilation o, const void * const i_pointer);
void *      ReservePointer              (IM3Compilation o);

pc_t        GetPC                       (IM3Compilation o);

d_m3EndExternC

#endif // m3_emit_h
