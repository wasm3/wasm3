//
//  m3_exec_defs.h
//
//  Created by Steven Massey on 5/1/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_exec_defs_h
#define m3_exec_defs_h

#include "m3_core.h"

d_m3BeginExternC

#define m3MemData(mem)              (u8*)(((M3MemoryHeader*)(mem))+1)
#define m3MemRuntime(mem)           (((M3MemoryHeader*)(mem))->runtime)
#define m3MemInfo(mem)              (&(((M3MemoryHeader*)(mem))->runtime->memory))

#if d_m3HasFloat

#   define d_m3OpSig                pc_t _pc, m3stack_t _sp, M3MemoryHeader * _mem, m3reg_t _r0, f64 _fp0
#   define d_m3OpArgs               _sp, _mem, _r0, _fp0
#   define d_m3OpAllArgs            _pc, _sp, _mem, _r0, _fp0
#   define d_m3OpDefaultArgs        0, 0.0
#   define d_m3ClearRegisters       _r0 = 0; _fp0 = 0.0;

#else

#   define d_m3OpSig                pc_t _pc, m3stack_t _sp, M3MemoryHeader * _mem, m3reg_t _r0
#   define d_m3OpArgs               _sp, _mem, _r0
#   define d_m3OpAllArgs            _pc, _sp, _mem, _r0
#   define d_m3OpDefaultArgs        0
#   define d_m3ClearRegisters       _r0 = 0;

#endif

typedef m3ret_t (vectorcall * IM3Operation) (d_m3OpSig);

#define d_m3RetSig                  static inline m3ret_t vectorcall
#define d_m3Op(NAME)                op_section d_m3RetSig op_##NAME (d_m3OpSig)

#define nextOpImpl()                ((IM3Operation)(* _pc))(_pc + 1, d_m3OpArgs)
#define jumpOpImpl(PC)              ((IM3Operation)(*  PC))( PC + 1, d_m3OpArgs)

#define nextOpDirect()              return nextOpImpl()
#define jumpOpDirect(PC)            return jumpOpImpl((pc_t)(PC))

d_m3EndExternC

#endif // m3_exec_defs_h
