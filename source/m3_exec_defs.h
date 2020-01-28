//
//  m3_exec_defs.h
//
//  Created by Steven Massey on 5/1/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_exec_defs_h
#define m3_exec_defs_h

#include "m3_core.h"

// actually this is "no tail calls" + "use less function args"
// Probably need to make this whole set of defines, plus some in m3_exec.h, overridable in an arch-specific header.
#define d_m3NoTailCalls 1

#ifndef d_m3NoTailCalls
#   define d_m3OpSig                pc_t _pc, u64 * _sp, M3MemoryHeader * _mem, m3reg_t _r0, f64 _fp0
#   define d_m3OpArgs               _sp, _mem, _r0, _fp0
#   define d_m3OpAllArgs            _pc, _sp, _mem, _r0, _fp0
#   define d_m3OpDefaultArgs        _mem, 666, NAN
#   define d_usesMemory(mem)
#   define d_usesFP()
#   define d_saveFP()
#   define d_updateMem(mem)         _mem = mem
#   define m3MemData(mem)           (u8*)((M3MemoryHeader*)(mem)+1)
typedef m3ret_t (vectorcall * IM3Operation) (d_m3OpSig);

#else // d_m3NoTailCalls

// Xtensa note: first two args are 32-bit pointers, 3rd and 4th are 64-bit values.
// In total they occupy the max. number of registers which can be used for function arguments on Xtensa
// Any extra args would have to go onto the stack.
// Unlike the !d_m3NoTailCalls case above, _mem becomes a global (but can be made thread local if needed).
#   define d_m3OpSig                pc_t _pc, u64 * _sp, m3reg_t _r0
#   define d_m3OpArgs               _sp, _r0
#   define d_m3OpAllArgs            _pc, _sp, _r0
#   define d_m3OpDefaultArgs        666

#   define m3MemData(mem)           (u8*)((M3MemoryHeader*)(mem)+1)
extern M3MemoryHeader * g_mem;
#   define d_usesMemory(mem)       M3MemoryHeader * mem = g_mem;
#   define d_updateMem(mem)        g_mem = mem

#ifdef __xtensa__

#   define d_usesFP()              f64 _fp0 __attribute__((unused)) = loadFP()
#   define d_saveFP()              saveFP(_fp0)

static inline f64 loadFP() {
    union {
        f64 fp;
        struct {
            u32 l;
            u32 h;
        };
    } res;
    __asm__ __volatile__("rur.F64R_LO %0":"=r"(res.l));
    __asm__  __volatile__("rur.F64R_HI %0":"=r"(res.h));
    return res.fp;
}

static inline void saveFP(f64 fp) {
    union {
        f64 fp;
        struct {
            u32 l;
            u32 h;
        };
    } arg;
    arg.fp = fp;
    __asm__  __volatile__("wur.F64R_LO %0"::"r"(arg.l));
    __asm__  __volatile__("wur.F64R_HI %0"::"r"(arg.h));
}
#else // __xtensa___

extern f64 g_fp0;

#   define d_usesFP()              f64 _fp0 __attribute__((unused)) = g_fp0
#   define d_saveFP()              g_fp0 = _fp0

#endif // __xtensa__

typedef struct {
    union {
        pc_t pc;
        m3ret_t err;
    };
    m3stack_t sp;
    m3reg_t r0;
} m3_ret_struct_t;

#define d_m3OpRetError(e)  return (m3_ret_struct_t) {.err=e, .sp=0, .r0 = 0}
#define d_m3OpRetTailCall(pc_)  return (m3_ret_struct_t) {.pc=pc_, .sp=_sp, .r0 = _r0}
typedef m3_ret_struct_t (vectorcall * IM3Operation) (d_m3OpSig);

#endif // d_m3NoTailCalls


#endif // m3_exec_defs_h
