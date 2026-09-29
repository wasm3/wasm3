//
//  m3_atomic.h
//
//  The primitives under the threads proposal's atomic accesses, and the few atomic
//  operations the engine keeps for itself.
//
//  The first kind take the address of a naturally aligned cell in linear memory - the
//  interpreter has checked that already - and work in host byte order; a big-endian
//  host swaps on the way in and out, as the ordinary loads and stores do. The width
//  of the cell is a value, its log2 from 0 to 3, and so is the operation of a
//  read-modify-write, so that one body serves every instruction of a kind. Values
//  travel as u64 and are cut to the width on the way into the cell. Every
//  read-modify-write returns the value the cell held before.
//
//  Without d_m3HasThreads nothing else can touch the cell, so these are plain reads
//  and writes. With it they are sequentially consistent, which is all the proposal
//  asks: a load and a store are one instruction, and a read-modify-write is a
//  compare-and-swap loop on the cell, which is correct at every width, on hosts of
//  either byte order, and without a native instruction for each operation.
//
//  The second kind are host-order values the engine shares between threads: a memory's
//  length, a reference count, a flag, a lock. They need no byte swap and no width
//  argument, and they cost nothing without d_m3HasThreads.
//

#ifndef m3_atomic_h
#define m3_atomic_h

#include "m3_core.h"

d_m3BeginExternC

#if d_m3HasAtomics

enum {
    c_atomicAdd,
    c_atomicSub,
    c_atomicAnd,
    c_atomicOr,
    c_atomicXor,
    c_atomicXchg
};

// What a read-modify-write does to the value the cell held
static inline
u64 m3_AtomicApply (u32 i_kind, u64 i_old, u64 i_value)
{
    switch (i_kind) {
    case c_atomicAdd: return i_old + i_value;
    case c_atomicSub: return i_old - i_value;
    case c_atomicAnd: return i_old & i_value;
    case c_atomicOr: return i_old | i_value;
    case c_atomicXor: return i_old ^ i_value;
    default: return i_value;
    }
}

#  if d_m3HasThreads

// Raw sequentially consistent operations on a cell of type T, which the rest is built
// from: m3_AtomicRawLoad_T, m3_AtomicRawStore_T and m3_AtomicRawCas_T, which replaces
// *io_expected with what the cell held when it fails.
#    if defined(__GNUC__) || defined(__clang__)

// the cells are bytes of linear memory reached through a wider type
#      define d_m3AtomicRaw(T)                                                                      \
          typedef T __attribute__ ((may_alias)) m3_Alias_##T;                                       \
                                                                                                    \
          static inline T m3_AtomicRawLoad_##T (const u8* i_cell)                                   \
          {                                                                                         \
              return __atomic_load_n ((const m3_Alias_##T*) i_cell, __ATOMIC_SEQ_CST);              \
          }                                                                                         \
                                                                                                    \
          static inline void m3_AtomicRawStore_##T (u8* o_cell, T i_value)                          \
          {                                                                                         \
              __atomic_store_n ((m3_Alias_##T*) o_cell, i_value, __ATOMIC_SEQ_CST);                 \
          }                                                                                         \
                                                                                                    \
          static inline bool m3_AtomicRawCas_##T (u8* io_cell, T* io_expected, T i_desired)         \
          {                                                                                         \
              return __atomic_compare_exchange_n ((m3_Alias_##T*) io_cell, io_expected, i_desired,  \
                                                  false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);       \
          }

#    elif defined(_MSC_VER)

#      include <intrin.h>

// The interlocked intrinsics are typed by the width: char, short, long, __int64
#      define d_m3InterlockedCas_u8(P, NEW, OLD)    _InterlockedCompareExchange8 ((volatile char*) (P), (char) (NEW), (char) (OLD))
#      define d_m3InterlockedCas_u16(P, NEW, OLD)   _InterlockedCompareExchange16 ((volatile short*) (P), (short) (NEW), (short) (OLD))
#      define d_m3InterlockedCas_u32(P, NEW, OLD)   _InterlockedCompareExchange ((volatile long*) (P), (long) (NEW), (long) (OLD))
#      define d_m3InterlockedCas_u64(P, NEW, OLD)   _InterlockedCompareExchange64 ((volatile __int64*) (P), (__int64) (NEW), (__int64) (OLD))
#      define d_m3InterlockedXchg_u8(P, V)          _InterlockedExchange8 ((volatile char*) (P), (char) (V))
#      define d_m3InterlockedXchg_u16(P, V)         _InterlockedExchange16 ((volatile short*) (P), (short) (V))
#      define d_m3InterlockedXchg_u32(P, V)         _InterlockedExchange ((volatile long*) (P), (long) (V))
#      if defined(_M_IX86)
// a 32-bit target has the compare-and-swap of a 64-bit cell and no exchange of one
static inline
__int64 m3_InterlockedExchange64 (volatile __int64* io_cell, __int64 i_value)
{
    __int64 old = 0;

    for (;;) {
        __int64 previous = _InterlockedCompareExchange64(io_cell, i_value, old);

        if (previous == old) {
            return old;
        }

        old = previous;
    }
}
#        define d_m3InterlockedXchg_u64(P, V)       m3_InterlockedExchange64 ((volatile __int64*) (P), (__int64) (V))
#      else
#        define d_m3InterlockedXchg_u64(P, V)       _InterlockedExchange64 ((volatile __int64*) (P), (__int64) (V))
#      endif

// A load that is not a write. On x86 an aligned load is atomic already, and carries the
// acquire a sequentially consistent load needs (the stores are exchanges, which is the
// other half), so it only has to keep the compiler from moving anything across it. A
// compare-and-swap of zero for zero would work anywhere, at the price of taking the cache
// line exclusively for every read - which is what the other targets still pay.
#      if defined(_M_X64) || defined(_M_IX86)
#        define d_m3PlainLoad(T, C)          (_ReadWriteBarrier (), *(const volatile T*) (C))
#        define d_m3InterlockedLoad_u8(C)    d_m3PlainLoad (u8, C)
#        define d_m3InterlockedLoad_u16(C)   d_m3PlainLoad (u16, C)
#        define d_m3InterlockedLoad_u32(C)   d_m3PlainLoad (u32, C)
#        if defined(_M_X64)
#          define d_m3InterlockedLoad_u64(C) d_m3PlainLoad (u64, C)
#        else
#          define d_m3InterlockedLoad_u64(C) d_m3InterlockedCas_u64 (C, 0, 0)
#        endif
#      else
#        define d_m3InterlockedLoad_u8(C)    d_m3InterlockedCas_u8 (C, 0, 0)
#        define d_m3InterlockedLoad_u16(C)   d_m3InterlockedCas_u16 (C, 0, 0)
#        define d_m3InterlockedLoad_u32(C)   d_m3InterlockedCas_u32 (C, 0, 0)
#        define d_m3InterlockedLoad_u64(C)   d_m3InterlockedCas_u64 (C, 0, 0)
#      endif

#      define d_m3AtomicRaw(T)                                                                      \
          static inline T m3_AtomicRawLoad_##T (const u8* i_cell)                                   \
          {                                                                                         \
              return (T) d_m3InterlockedLoad_##T (i_cell);                                      \
          }                                                                                         \
                                                                                                    \
          static inline void m3_AtomicRawStore_##T (u8* o_cell, T i_value)                          \
          {                                                                                         \
              (void) d_m3InterlockedXchg_##T (o_cell, i_value);                                     \
          }                                                                                         \
                                                                                                    \
          static inline bool m3_AtomicRawCas_##T (u8* io_cell, T* io_expected, T i_desired)         \
          {                                                                                         \
              T previous = (T) d_m3InterlockedCas_##T (io_cell, i_desired, *io_expected);           \
              bool swapped = (previous == *io_expected);                                            \
              *io_expected = previous;                                                              \
              return swapped;                                                                       \
          }

#    else
#      error "d_m3HasThreads needs atomic operations this compiler does not provide"
#    endif

d_m3AtomicRaw(u8);
d_m3AtomicRaw(u16);
d_m3AtomicRaw(u32);
d_m3AtomicRaw(u64);

#    define d_m3AtomicTyped(T)                                                    \
        static inline T m3_AtomicLoad_##T (const u8* i_cell)                      \
        {                                                                         \
            T value = m3_AtomicRawLoad_##T (i_cell);                              \
            M3_BSWAP_##T (value);                                                 \
            return value;                                                         \
        }                                                                         \
                                                                                  \
        static inline void m3_AtomicStore_##T (u8* o_cell, T i_value)             \
        {                                                                         \
            M3_BSWAP_##T (i_value);                                               \
            m3_AtomicRawStore_##T (o_cell, i_value);                              \
        }                                                                         \
                                                                                  \
        static inline u64 m3_AtomicRmw_##T (u8* io_cell, u32 i_kind, u64 i_value) \
        {                                                                         \
            T raw = m3_AtomicRawLoad_##T (io_cell);                               \
                                                                                  \
            for (;;) {                                                            \
                T old = raw;                                                      \
                M3_BSWAP_##T (old);                                               \
                                                                                  \
                T updated = (T) m3_AtomicApply (i_kind, old, i_value);            \
                M3_BSWAP_##T (updated);                                           \
                                                                                  \
                if (m3_AtomicRawCas_##T (io_cell, & raw, updated)) {              \
                    return old;                                                   \
                }                                                                 \
            }                                                                     \
        }                                                                         \
                                                                                  \
        static inline u64 m3_AtomicCmpxchg_##T (u8* io_cell, u64 i_expected, u64 i_replacement) \
        {                                                                         \
            T expected = (T) i_expected;                                          \
            T replacement = (T) i_replacement;                                    \
            M3_BSWAP_##T (expected);                                              \
            M3_BSWAP_##T (replacement);                                           \
                                                                                  \
            m3_AtomicRawCas_##T (io_cell, & expected, replacement);               \
                                                                                  \
            M3_BSWAP_##T (expected);                                              \
            return expected;                                                      \
        }

#  else // d_m3HasThreads

#    define d_m3AtomicTyped(T)                                                    \
        static inline T m3_AtomicLoad_##T (const u8* i_cell)                      \
        {                                                                         \
            T value;                                                              \
            memcpy (& value, i_cell, sizeof (value));                             \
            M3_BSWAP_##T (value);                                                 \
            return value;                                                         \
        }                                                                         \
                                                                                  \
        static inline void m3_AtomicStore_##T (u8* o_cell, T i_value)             \
        {                                                                         \
            M3_BSWAP_##T (i_value);                                               \
            memcpy (o_cell, & i_value, sizeof (i_value));                         \
        }                                                                         \
                                                                                  \
        static inline u64 m3_AtomicRmw_##T (u8* io_cell, u32 i_kind, u64 i_value) \
        {                                                                         \
            T old = m3_AtomicLoad_##T (io_cell);                                  \
            m3_AtomicStore_##T (io_cell, (T) m3_AtomicApply (i_kind, old, i_value)); \
            return old;                                                           \
        }                                                                         \
                                                                                  \
        static inline u64 m3_AtomicCmpxchg_##T (u8* io_cell, u64 i_expected, u64 i_replacement) \
        {                                                                         \
            T old = m3_AtomicLoad_##T (io_cell);                                  \
            if (old == (T) i_expected) {                                          \
                m3_AtomicStore_##T (io_cell, (T) i_replacement);                  \
            }                                                                     \
            return old;                                                           \
        }

#  endif // d_m3HasThreads

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
    switch (i_log2Width) {
    case 0: return m3_AtomicRmw_u8(io_cell, i_kind, i_value);
    case 1: return m3_AtomicRmw_u16(io_cell, i_kind, i_value);
    case 2: return m3_AtomicRmw_u32(io_cell, i_kind, i_value);
    default: return m3_AtomicRmw_u64(io_cell, i_kind, i_value);
    }
}

// The value compared against is cut to the width of the cell
static inline
u64 m3_AtomicCmpxchgW (u8* io_cell, u32 i_log2Width, u64 i_expected, u64 i_replacement)
{
    switch (i_log2Width) {
    case 0: return m3_AtomicCmpxchg_u8(io_cell, i_expected, i_replacement);
    case 1: return m3_AtomicCmpxchg_u16(io_cell, i_expected, i_replacement);
    case 2: return m3_AtomicCmpxchg_u32(io_cell, i_expected, i_replacement);
    default: return m3_AtomicCmpxchg_u64(io_cell, i_expected, i_replacement);
    }
}

#endif // d_m3HasAtomics


//---------------------------------------------------------------------------------------------------------------------
// What the engine shares between threads
//---------------------------------------------------------------------------------------------------------------------

#if d_m3HasThreads

// A size read on every memory access, so relaxed; written under a lock, so released
static inline
size_t m3_SharedLoadSize (const size_t* i_size)
{
#  if defined(__GNUC__) || defined(__clang__)
    return __atomic_load_n(i_size, __ATOMIC_RELAXED);
#  else
    return *(const volatile size_t*)i_size;
#  endif
}

static inline
void m3_SharedStoreSize (size_t* o_size, size_t i_value)
{
#  if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(o_size, i_value, __ATOMIC_RELEASE);
#  else
    *(volatile size_t*)o_size = i_value;
#  endif
}

// A word that is settled once and read after: written with everything before it in view
static inline
u32 m3_SharedLoad32 (const u32* i_value)
{
    return m3_AtomicRawLoad_u32((const u8*)i_value);
}

static inline
void m3_SharedStore32 (u32* o_value, u32 i_value)
{
    m3_AtomicRawStore_u32((u8*)o_value, i_value);
}

static inline
u64 m3_SharedLoad64 (const u64* i_value)
{
    return m3_AtomicRawLoad_u64((const u8*)i_value);
}

static inline
void m3_SharedStore64 (u64* o_value, u64 i_value)
{
    m3_AtomicRawStore_u64((u8*)o_value, i_value);
}

// Both answer the count after the change
static inline
u32 m3_SharedIncrement (u32* io_count)
{
#  if defined(__GNUC__) || defined(__clang__)
    return __atomic_add_fetch(io_count, 1, __ATOMIC_SEQ_CST);
#  else
    return (u32)_InterlockedIncrement((volatile long*)io_count);
#  endif
}

static inline
u32 m3_SharedDecrement (u32* io_count)
{
#  if defined(__GNUC__) || defined(__clang__)
    return __atomic_sub_fetch(io_count, 1, __ATOMIC_SEQ_CST);
#  else
    return (u32)_InterlockedDecrement((volatile long*)io_count);
#  endif
}

// A flag another thread, or a signal handler, may set while this one reads it
static inline
bool m3_SharedLoadFlag (const volatile bool* i_flag)
{
#  if defined(__GNUC__) || defined(__clang__)
    return __atomic_load_n(i_flag, __ATOMIC_RELAXED);
#  else
    return *i_flag;
#  endif
}

static inline
void m3_SharedStoreFlag (volatile bool* o_flag, bool i_value)
{
#  if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(o_flag, i_value, __ATOMIC_RELAXED);
#  else
    *o_flag = i_value;
#  endif
}

// For process-wide state that has no owner to hold a mutex for it and is never held
// for long: the arena of guarded memories, the installing of signal handlers. Unlike
// a mutex from the host it needs no initializing, only to start as zero.
typedef u32 M3SpinLock;

// Tells the processor this is a spin loop, so that it does not run ahead of the lock it
// is waiting for or burn the power to
static inline
void m3_CpuRelax (void)
{
#  if defined(__GNUC__) || defined(__clang__)
#    if defined(__x86_64__) || defined(__i386__)
    // written out: the builtin wants SSE2 for a 32-bit target, and the instruction
    // itself is a plain no-op on a processor that has never heard of it
    __asm__ volatile("pause" ::: "memory");
#    elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#    else
    __asm__ volatile("" ::: "memory");
#    endif
#  elif defined(_MSC_VER)
#    if defined(_M_X64) || defined(_M_IX86)
    _mm_pause();
#    elif defined(_M_ARM) || defined(_M_ARM64)
    __yield();
#    endif
#  endif
}

// Reads the lock until it looks free before trying to take it, so that waiting is a read
// from the cache and not a stream of writes to the line every other waiter is reading
static inline
void m3_SpinLock (M3SpinLock* io_lock)
{
    for (;;) {
        u32 expected = 0;

        if (m3_AtomicRawCas_u32((u8*)io_lock, &expected, 1)) {
            return;
        }

        while (m3_AtomicRawLoad_u32((const u8*)io_lock) != 0) {
            m3_CpuRelax();
        }
    }
}

static inline
void m3_SpinUnlock (M3SpinLock* io_lock)
{
    m3_AtomicRawStore_u32((u8*)io_lock, 0);
}

#else // d_m3HasThreads

#  define m3_SharedLoadSize(P)        (*(P))
#  define m3_SharedStoreSize(P, V)    (*(P) = (V))
#  define m3_SharedLoad32(P)          (*(P))
#  define m3_SharedStore32(P, V)      (*(P) = (V))
#  define m3_SharedLoad64(P)          (*(P))
#  define m3_SharedStore64(P, V)      (*(P) = (V))
#  define m3_SharedLoadFlag(P)        (*(P))
#  define m3_SharedStoreFlag(P, V)    (*(P) = (V))

typedef u32 M3SpinLock;

#  define m3_SpinLock(P)              ((void)(P))
#  define m3_SpinUnlock(P)            ((void)(P))

#endif // d_m3HasThreads

d_m3EndExternC

#endif // m3_atomic_h
