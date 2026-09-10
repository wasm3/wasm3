//
//  m3_deterministic.h
//
//  Copyright © 2026 Volodymyr Shymanskyy. All rights reserved.
//

#ifndef m3_deterministic_h
#define m3_deterministic_h

#include "m3_env.h"

d_m3BeginExternC

// The clock and the entropy the host APIs answer out of under
// d_m3DeterministicProfile, instead of out of the machine they happen to be
// running on. Both live on M3Runtime, so two runtimes replay independently and a
// fresh one replays the same way as the last.
//
// The profile itself is the language half - see d_m3DeterministicProfile in
// m3_config.h. This is the other half, what a module reads through its imports,
// and it is wasm3's own rather than the proposal's.

// How far the clock moves on each reading, and so the finest interval it can tell
// apart. A guest that waits by watching the clock has to see it move, and this is
// how fast it does.
#define d_m3DeterministicTickNs         UINT64_C(1000000)     // 1ms

// Where the entropy stream starts. Nothing about the value matters except that it
// is written down, since a replay has to produce the same bytes.
#define d_m3DeterministicSeed           UINT64_C(0x9E3779B97F4A7C15)

// Where the deterministic wall clock starts: 2020-01-01T00:00:00Z. Any fixed instant
// would do; a real one keeps a guest that formats the date from printing something
// absurd. The monotonic clocks start at zero, as they would anyway.
#define d_m3WasiDeterministicEpochNs    UINT64_C(1577836800000000000)

#if d_m3DeterministicProfile

// Start a runtime's clock and entropy where every other runtime starts them, which
// is what makes a run replayable by making a fresh runtime.
void Deterministic_InitRuntime (IM3Runtime io_runtime);

// The clock counts from the moment the runtime was made, leaving it to the host API
// to say what instant that was. Reading it advances it, so that a guest spinning
// until time passes gets there.
u64  Deterministic_Time (IM3Runtime io_runtime);
void Deterministic_Advance (IM3Runtime io_runtime, u64 i_nanoseconds);
void Deterministic_Random (IM3Runtime io_runtime, void* o_buf, size_t i_length);

#else

// Nothing to seed: the fields these would set are not on M3Runtime in this build
#  define Deterministic_InitRuntime(RT)    do {} while (0)

#endif

d_m3EndExternC

#endif /* m3_deterministic_h */
