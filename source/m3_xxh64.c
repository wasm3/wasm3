//
//  m3_xxh64.c
//
//  XXH64. A port of the reference implementation of xxHash 0.8.3 (BSD-2-Clause)
//  https://github.com/Cyan4973/xxHash
//
//  Digests match the reference on every host. The algorithm is defined over
//  little-endian reads, which is what the accessor below performs whatever the
//  host's byte order, and everything else is 64-bit arithmetic.
//

#include "m3_core.h"

#if d_m3HasSnapshots


//---------------------------------------------------------------------------------------------------------------------------------
//  constants and primitives
//---------------------------------------------------------------------------------------------------------------------------------

#  define XXH_PRIME64_1    0x9E3779B185EBCA87ULL
#  define XXH_PRIME64_2    0xC2B2AE3D27D4EB4FULL
#  define XXH_PRIME64_3    0x165667B19E3779F9ULL
#  define XXH_PRIME64_4    0x85EBCA77C2B2AE63ULL
#  define XXH_PRIME64_5    0x27D4EB2F165667C5ULL

#  define XXH_STRIPE_LEN   32     // must match Xxh64.buffer

static inline
u32 XxhRead32 (const void* i_ptr)
{
    u32 value;
    memcpy(&value, i_ptr, sizeof(value));
    M3_BSWAP_u32(value);
    return value;
}

static inline
u64 XxhRead64 (const void* i_ptr)
{
    u64 value;
    memcpy(&value, i_ptr, sizeof(value));
    M3_BSWAP_u64(value);
    return value;
}

static inline
u64 XxhRotl64 (u64 i_value, u32 i_count)
{
    return (i_value << i_count) | (i_value >> (64 - i_count));
}

// One lane taking one 8-byte word
static inline
u64 XxhRound (u64 i_acc, u64 i_input)
{
    i_acc += i_input * XXH_PRIME64_2;
    i_acc = XxhRotl64(i_acc, 31);
    return i_acc * XXH_PRIME64_1;
}

// ...and one lane folded into the hash at the end
static inline
u64 XxhMergeRound (u64 i_acc, u64 i_value)
{
    i_acc ^= XxhRound(0, i_value);
    return i_acc * XXH_PRIME64_1 + XXH_PRIME64_4;
}

static inline
u64 XxhAvalanche (u64 i_hash)
{
    i_hash ^= i_hash >> 33;
    i_hash *= XXH_PRIME64_2;
    i_hash ^= i_hash >> 29;
    i_hash *= XXH_PRIME64_3;
    i_hash ^= i_hash >> 32;
    return i_hash;
}


//---------------------------------------------------------------------------------------------------------------------------------
//  internals
//---------------------------------------------------------------------------------------------------------------------------------

// Takes whole 32-byte stripes and answers where in the input it stopped
static
const u8* XxhConsumeStripes (u64* io_acc, const u8* i_input, size_t i_size)
{
    const u8* input = i_input;
    const u8* limit = i_input + i_size - (XXH_STRIPE_LEN - 1);

    do {
        for (size_t lane = 0; lane < 4; ++lane) {
            io_acc[lane] = XxhRound(io_acc[lane], XxhRead64(input));
            input += 8;
        }
    } while (input < limit);

    return input;
}

static
u64 XxhMergeAccs (const u64* i_acc)
{
    u64 hash = XxhRotl64(i_acc[0], 1) + XxhRotl64(i_acc[1], 7) + XxhRotl64(i_acc[2], 12) + XxhRotl64(i_acc[3], 18);

    for (size_t lane = 0; lane < 4; ++lane) {
        hash = XxhMergeRound(hash, i_acc[lane]);
    }

    return hash;
}

// The tail: whatever is left over after the last whole stripe, down to
// single bytes
static
u64 XxhFinalize (u64 i_hash, const u8* i_input, size_t i_size)
{
    size_t size = i_size & (XXH_STRIPE_LEN - 1);

    while (size >= 8) {
        i_hash ^= XxhRound(0, XxhRead64(i_input));
        i_hash = XxhRotl64(i_hash, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
        i_input += 8;
        size -= 8;
    }

    if (size >= 4) {
        i_hash ^= (u64)XxhRead32(i_input) * XXH_PRIME64_1;
        i_hash = XxhRotl64(i_hash, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
        i_input += 4;
        size -= 4;
    }

    while (size > 0) {
        i_hash ^= (*i_input++) * XXH_PRIME64_5;
        i_hash = XxhRotl64(i_hash, 11) * XXH_PRIME64_1;
        --size;
    }

    return XxhAvalanche(i_hash);
}


//---------------------------------------------------------------------------------------------------------------------------------
//  API
//---------------------------------------------------------------------------------------------------------------------------------

void Xxh64_Init (Xxh64* o_state, u64 i_seed)
{
    o_state->acc[0] = i_seed + XXH_PRIME64_1 + XXH_PRIME64_2;
    o_state->acc[1] = i_seed + XXH_PRIME64_2;
    o_state->acc[2] = i_seed;                   // the digest reads the seed back out of this one
    o_state->acc[3] = i_seed - XXH_PRIME64_1;

    o_state->totalLength  = 0;
    o_state->bufferLength = 0;
}

void Xxh64_Update (Xxh64* io_state, const void* i_data, size_t i_size)
{
    if (not i_data) {
        return;
    }

    const u8* input = (const u8*)i_data;
    const u8* end   = input + i_size;

    io_state->totalLength += i_size;

    if (i_size < (size_t)(XXH_STRIPE_LEN - io_state->bufferLength)) {
        memcpy(io_state->buffer + io_state->bufferLength, input, i_size);
        io_state->bufferLength += (u32)i_size;
        return;
    }

    // fill up what the buffer already holds, and consume that
    if (io_state->bufferLength) {
        size_t loadSize = XXH_STRIPE_LEN - io_state->bufferLength;

        memcpy(io_state->buffer + io_state->bufferLength, input, loadSize);
        input += loadSize;

        XxhConsumeStripes(io_state->acc, io_state->buffer, XXH_STRIPE_LEN);
        io_state->bufferLength = 0;
    }

    if ((size_t)(end - input) >= XXH_STRIPE_LEN) {
        input = XxhConsumeStripes(io_state->acc, input, (size_t)(end - input));
    }

    if (input < end) {
        memcpy(io_state->buffer, input, (size_t)(end - input));
        io_state->bufferLength = (u32)(end - input);
    }
}

u64 Xxh64_Digest (const Xxh64* i_state)
{
    // Short input never reached the accumulators, so it is hashed from the seed
    // alone - which acc[2] still holds, untouched by Xxh64_Init.
    u64 hash = (i_state->totalLength >= XXH_STRIPE_LEN) ? XxhMergeAccs(i_state->acc)
                                                        : i_state->acc[2] + XXH_PRIME64_5;

    hash += i_state->totalLength;

    return XxhFinalize(hash, i_state->buffer, (size_t)i_state->totalLength);
}

#endif // d_m3HasSnapshots
