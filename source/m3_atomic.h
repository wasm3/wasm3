//
//  m3_atomic.h
//
//  The primitives under the threads proposal's atomic accesses. Each takes the
//  address of a naturally aligned cell in linear memory - the interpreter has
//  checked that already - and works in host byte order; a big-endian host swaps
//  on the way in and out, as the ordinary loads and stores do.
//
//  The width of the cell is a value, its log2 from 0 to 3, and so is the operation of
//  a read-modify-write, so that one body serves every instruction of a kind. Values
//  travel as u64 and are cut to the width on the way into the cell.
//
//  Every read-modify-write returns the value the cell held before.
//
//  With one thread the accesses only have to be correct in sequence, so these are
//  plain reads and writes.
//

#ifndef m3_atomic_h
#define m3_atomic_h

#include "m3_core.h"

#if d_m3HasAtomics

d_m3BeginExternC

enum {
    c_atomicAdd,
    c_atomicSub,
    c_atomicAnd,
    c_atomicOr,
    c_atomicXor,
    c_atomicXchg
};

#  define d_m3AtomicTyped(T)                                            \
    static inline T m3_AtomicLoad_##T (const u8* i_cell)                \
    {                                                                   \
        T value;                                                        \
        memcpy (& value, i_cell, sizeof (value));                       \
        M3_BSWAP_##T (value);                                           \
        return value;                                                   \
    }                                                                   \
                                                                        \
    static inline void m3_AtomicStore_##T (u8* o_cell, T i_value)       \
    {                                                                   \
        M3_BSWAP_##T (i_value);                                         \
        memcpy (o_cell, & i_value, sizeof (i_value));                   \
    }

d_m3AtomicTyped(u8);
d_m3AtomicTyped(u16);
d_m3AtomicTyped(u32);
d_m3AtomicTyped(u64);

static inline
u64 m3_AtomicLoadW (const u8* i_cell, u32 i_log2Width)
{
    switch (i_log2Width) {
    case 0: return m3_AtomicLoad_u8(i_cell);
    case 1: return m3_AtomicLoad_u16(i_cell);
    case 2: return m3_AtomicLoad_u32(i_cell);
    default: return m3_AtomicLoad_u64(i_cell);
    }
}

static inline
void m3_AtomicStoreW (u8* o_cell, u32 i_log2Width, u64 i_value)
{
    switch (i_log2Width) {
    case 0: m3_AtomicStore_u8(o_cell, (u8)i_value); break;
    case 1: m3_AtomicStore_u16(o_cell, (u16)i_value); break;
    case 2: m3_AtomicStore_u32(o_cell, (u32)i_value); break;
    default: m3_AtomicStore_u64(o_cell, i_value); break;
    }
}

static inline
u64 m3_AtomicRmwW (u8* io_cell, u32 i_log2Width, u32 i_kind, u64 i_value)
{
    u64 old = m3_AtomicLoadW(io_cell, i_log2Width);
    u64 updated;

    switch (i_kind) {
    case c_atomicAdd: updated = old + i_value; break;
    case c_atomicSub: updated = old - i_value; break;
    case c_atomicAnd: updated = old & i_value; break;
    case c_atomicOr: updated = old | i_value; break;
    case c_atomicXor: updated = old ^ i_value; break;
    default: updated = i_value; break;
    }

    m3_AtomicStoreW(io_cell, i_log2Width, updated);
    return old;
}

static inline
u64 m3_AtomicCmpxchgW (u8* io_cell, u32 i_log2Width, u64 i_expected, u64 i_replacement)
{
    u64 old = m3_AtomicLoadW(io_cell, i_log2Width);
    u64 mask = (i_log2Width == 3) ? ~(u64)0 : (((u64)1 << (8u << i_log2Width)) - 1);

    if (old == (i_expected & mask)) {
        m3_AtomicStoreW(io_cell, i_log2Width, i_replacement);
    }
    return old;
}

d_m3EndExternC

#endif // d_m3HasAtomics

#endif // m3_atomic_h
