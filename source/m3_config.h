//
//  m3_config.h
//
//  Created by Steven Massey on 5/4/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_config_h
#define m3_config_h

#include "m3_config_platforms.h"

// general --------------------------------------------------------------------

#ifndef d_m3CodePageAlignSize
#  define d_m3CodePageAlignSize                32*1024
#endif

#ifndef d_m3MaxFunctionStackHeight
#  define d_m3MaxFunctionStackHeight           8000    // max: 32768
#endif

#ifndef d_m3MaxLinearMemoryPages
#  define d_m3MaxLinearMemoryPages             65536
#endif

#ifndef d_m3MaxFunctionSlots
#  define d_m3MaxFunctionSlots                 ((d_m3MaxFunctionStackHeight)*2)
#endif

#ifndef d_m3ValStack                                   // validator operand and local type stacks:
#  define d_m3ValStack                         (d_m3MaxFunctionStackHeight)       // the same operand stack the compiler bounds
#endif

#ifndef d_m3ValCtrlDepth                               // validator block nesting depth. Each frame is bigger than an
#  define d_m3ValCtrlDepth                     ((d_m3MaxFunctionStackHeight)/4)   // operand entry, so this dominates the validator's stack usage
#endif

#ifndef d_m3MaxConstantTableSize
#  define d_m3MaxConstantTableSize             120
#endif

#ifndef d_m3MaxDuplicateFunctionImpl
#  define d_m3MaxDuplicateFunctionImpl         3
#endif

#ifndef d_m3VerboseErrorMessages
#  define d_m3VerboseErrorMessages             1
#endif

#ifndef d_m3FixedHeap
#  define d_m3FixedHeap                        false
//# define d_m3FixedHeap                       (32*1024)
#endif

#ifndef d_m3FixedHeapAlign
#  define d_m3FixedHeapAlign                   16
#endif

#ifndef d_m3Use32BitSlots
#  define d_m3Use32BitSlots                    1
#endif

#ifndef d_m3ProfilerSlotMask
#  define d_m3ProfilerSlotMask                 0xFFFF
#endif

#ifndef d_m3RecordBacktraces
#  define d_m3RecordBacktraces                 0
#endif

#ifndef d_m3EnableExceptionBreakpoint
#  define d_m3EnableExceptionBreakpoint        0       // see m3_exception.h
#endif

// Backtraces and structured traces need op_Entry to still be around when the function
// body returns, so it can't tail-call into it.  Everywhere else it can, which keeps the
// native stack flat across calls -- and is what makes return_call actually iterative.
#ifndef d_m3EntryKeepsFrame
#  define d_m3EntryKeepsFrame                  (d_m3RecordBacktraces || (d_m3EnableStrace >= 2))
#endif

// Whether return_call/return_call_indirect can reuse the caller's frame.  Reusing it stops
// the m3 stack from growing, so it's only safe where the native stack doesn't grow either
// -- otherwise runaway tail recursion would blow the native stack with nothing to trap it.
// Where that doesn't hold they compile to a plain call followed by a return: still
// correct, just not iterative, and the m3 stack keeps overflowing (and trapping) first.
#ifndef d_m3CanTailCall
#  define d_m3CanTailCall                      (!d_m3EntryKeepsFrame && M3_GUARANTEED_TAIL_CALL)
#endif


// profiling and tracing ------------------------------------------------------

#ifndef d_m3EnableOpProfiling
#  define d_m3EnableOpProfiling                0       // opcode usage counters
#endif

#ifndef d_m3EnableOpTracing
#  define d_m3EnableOpTracing                  0       // only works with DEBUG
#endif

#ifndef d_m3EnableWasiTracing
#  define d_m3EnableWasiTracing                0
#endif

#ifndef d_m3EnableStrace
#  define d_m3EnableStrace                     0       // 1 - trace exported function calls
// 2 - trace all calls (structured)
// 3 - all calls + loops + memory operations
#endif


// logging --------------------------------------------------------------------

#ifndef d_m3LogParse
#  define d_m3LogParse                         0       // .wasm binary decoding info
#endif

#ifndef d_m3LogModule
#  define d_m3LogModule                        0       // wasm module info
#endif

#ifndef d_m3LogCompile
#  define d_m3LogCompile                       0       // wasm -> metacode generation phase
#endif

#ifndef d_m3LogWasmStack
#  define d_m3LogWasmStack                     0       // dump the wasm stack when pushed or popped
#endif

#ifndef d_m3LogEmit
#  define d_m3LogEmit                          0       // metacode generation info
#endif

#ifndef d_m3LogCodePages
#  define d_m3LogCodePages                     0       // dump metacode pages when released
#endif

#ifndef d_m3LogRuntime
#  define d_m3LogRuntime                       0       // higher-level runtime information
#endif

#ifndef d_m3LogNativeStack
#  define d_m3LogNativeStack                   0       // track the memory usage of the C-stack
#endif

#ifndef d_m3LogHeapOps
#  define d_m3LogHeapOps                       0       // track heap usage
#endif

#ifndef d_m3LogTimestamps
#  define d_m3LogTimestamps                    0       // track timestamps on heap logs
#endif

// other ----------------------------------------------------------------------

#ifndef d_m3HasFloat
#  define d_m3HasFloat                         1       // implement floating point ops
#endif

// The deterministic profile: the same module fed the same input produces the same
// run, wherever and whenever it is run.
//
// A profile in the WebAssembly sense is a subset of the language that an ecosystem
// picks once, for all of its modules - not something a module or a call turns on -
// which is why this is a build option and not a runtime one. See
// https://github.com/WebAssembly/profiles. That proposal names the deterministic
// profile as the marker set {N,T}: it excludes the constructs whose behaviour is
// nondeterministic, and threading.
//
// What that comes to here:
//
//   N   the one rule the proposal spells out is that a NaN result is normalized to
//       the canonical NaN, which is d_m3CanonicalNaN below and is turned on by this.
//       The other N-marked constructs are relaxed SIMD, which wasm3 does not
//       implement at all, so there is nothing to exclude.
//   T   threads and atomics, which wasm3 does not implement either. Whoever adds
//       them has to keep them out of this profile.
//
// The proposal is about the language and stops at the module's edge. What a module
// reads through its imports - the clock, entropy, whether a wait actually waits - is
// nondeterministic in exactly the way that matters and is nobody's profile, so this
// option covers it too: the WASI clock counts from a fixed instant in fixed steps,
// random_get is a stream from a fixed seed, and poll_oneoff stops waiting on
// anything real. That half is wasm3's own, not the proposal's.
//
// Two limits belong beside it and stay separate because each is useful alone:
// m3_SetGasLimit, so that a runaway module stops after the same amount of work
// everywhere, and M3Runtime.memoryLimit, so that memory.grow fails at the same point
// rather than wherever the host allocator gives out.
#ifndef d_m3DeterministicProfile
#  define d_m3DeterministicProfile             0
#endif

// Where a float operation's result is a NaN, the spec lets the host choose its sign
// and payload, and different architectures choose differently: the same module
// produces different bits on x86, on ARM and on MIPS. With this on, every such
// result is replaced by the one canonical NaN, so those bits are the same
// everywhere.
//
// Follows the deterministic profile, which is the thing that needs it, and can be
// set on its own by a build that wants the float behaviour and nothing else. It
// costs a compare and a predictable branch on each of the float operations the spec
// calls arithmetic. It changes nothing else - a canonical NaN satisfies every
// assertion the spec suite makes about an arithmetic NaN - and abs, neg and copysign
// keep carrying their operand's payload through, as the spec defines them to.
#ifndef d_m3CanonicalNaN
#  define d_m3CanonicalNaN                     d_m3DeterministicProfile
#endif

// Issue the dispatch load for the next operation as soon as this one's immediates are
// consumed, rather than at the point of the jump that consumes it, so the operation's
// own work covers the load latency. See the comment in m3_exec.h.
#ifndef d_m3PreloadNextOp
#  define d_m3PreloadNextOp                    0
#endif

#ifndef d_m3FoldSetLocal
#  define d_m3FoldSetLocal                     1       // fuse a value-producing op with an immediately
#endif                                                 // following local.set by retro-patching its destination

#ifndef d_m3FuseBranch
#  define d_m3FuseBranch                       1       // fuse a compare with an immediately following
#endif                                                 // br_if/if into one op (needs d_m3FoldSetLocal)
#if d_m3FuseBranch && !d_m3FoldSetLocal
#  undef d_m3FuseBranch
#  define d_m3FuseBranch                       0       // the fusion candidate tracking lives in the fold machinery
#endif

#if !d_m3HasFloat && !defined(d_m3NoFloatDynamic)
#  define d_m3NoFloatDynamic                   1       // if no floats, do not fail until flops are actually executed
#endif

// funcref/externref values, the table instructions and multiple tables.
// Without it a module may still declare one funcref table and use call_indirect.
#ifndef d_m3HasRefTypes
#  define d_m3HasRefTypes                      1       // implement the reference types proposal
#endif

// i32/i64 add, sub and mul inside constant expressions, so a global, data or
// element offset can be computed from an imported global instead of a literal.
#ifndef d_m3HasExtendedConst
#  define d_m3HasExtendedConst                 1       // implement the extended constant expressions proposal
#endif

// More than one linear memory per module, each memory instruction naming the
// one it addresses. Memory 0 keeps the fast path -- the interpreter carries it
// in the _mem register -- and the others are reached by swapping that register
// around the access, so the cost lands only on modules that use them.
#ifndef d_m3HasMultiMemory
#  define d_m3HasMultiMemory                   1       // implement the multiple memories proposal
#endif

// 64-bit linear memories: a memory declares whether it is addressed by i32 or
// i64, and every instruction that names it takes operands of that type. The
// address space wasm3 can actually back is far smaller than 2^64 either way --
// see d_m3AddressLimit -- so this buys the addressing, not the range.
#ifndef d_m3HasMemory64
#  define d_m3HasMemory64                      1       // implement the memory64 proposal
#endif

// The import section's compact encodings: one module name shared by a run of
// imports, optionally with one externtype shared as well. Decoding only - a
// module means exactly what it would spelled out the long way.
#ifndef d_m3HasCompactImports
#  define d_m3HasCompactImports                1       // implement the compact import section proposal
#endif

// The exception handling proposal: a tag section, the exnref value type, and
// the try_table / throw / throw_ref instructions. Exceptions unwind by riding
// the same m3ret_t return path traps already use, so the cost when no module
// throws is a handful of branches.
//
// Four things it does not do:
//   - an imported tag is a fresh tag, not an alias of the exporting module's,
//     because wasm3 links imports against host functions rather than against
//     other modules. An exception thrown through an imported tag never matches
//     a catch clause naming it.
//   - exception objects belong to the runtime. One caught without being reified
//     is released at the catch; the rest are held until the outermost m3_Call
//     returns, which is the last point an exnref can still be reached from the
//     Wasm stack. One parked in a global or a table outlives that and dangles;
//     there is no collector to say otherwise.
//   - only the exnref shorthand is understood, not the (ref exn) / (ref null
//     exn) spellings, which need the exn heap type alongside typed function
//     references.
//   - entering a try region costs a native frame that is only given back when
//     the enclosing function returns or an enclosing loop comes round, so a long
//     straight-line run of try blocks in one function trades native stack for it.
//     d_m3MaxNativeStack bounds this the same way it bounds call depth.
#ifndef d_m3HasExceptionHandling
#  define d_m3HasExceptionHandling             1
#endif

// (ref $t) and (ref null $t), call_ref and the rest of the typed function
// references proposal. Off by default: it is not finished, and it widens the
// value type from one byte to two wherever the compiler carries one.
#ifndef d_m3HasTypedRefs
#  define d_m3HasTypedRefs                     0
#endif

// Gas metering. Once m3_SetGasLimit has armed a runtime, the compiler
// instruments the function bodies it compiles from then on: each straight-line
// segment of a body is charged, before any of it runs, for the instructions it
// is about to execute, and execution traps once the budget is gone. The
// segmentation and the per-opcode costs follow ewasm's metering design.
#ifndef d_m3HasGasMetering
#  define d_m3HasGasMetering                   1
#endif

#ifndef d_m3EnableValidation
#  define d_m3EnableValidation                 1       // pre-pass bytecode type validation
#endif

#ifndef d_m3SkipStackCheck
#  define d_m3SkipStackCheck                   0       // skip stack overrun checks
#endif

#ifndef d_m3MaxNativeStack
// native C-stack budget (bytes) available to Wasm execution. A recursive
// Wasm module builds up native call frames (op_Call -> op_Entry -> ...);
// once this budget is exhausted the interpreter traps instead of
// overflowing the real C stack. 0 disables.
//
// This is a ceiling, not a promise: where the system can be asked what the
// calling thread's stack actually is (m3_HostStackBase), a budget that would
// reach past the bottom of it is cut down to what is really there, less
// d_m3NativeStackMargin. That is what keeps the default safe on a thread
// smaller than it - a 256KiB worker, say - without every embedder having to
// know to lower it.
#  define d_m3MaxNativeStack                   ((8 * 1024 * 1024) - (128 * 1024))
#endif

#ifndef d_m3NativeStackMargin
// How much of a measured stack to leave below the mark. The mark is only tested
// between calls, so everything after the last test still has to fit: the rest of
// the op that traps, the interpreter frames unwinding behind it, and any host
// function already on the stack. Only used where the stack was measured rather
// than assumed; a stack too small to spare this much gives up a quarter of
// itself instead.
#  define d_m3NativeStackMargin                (64 * 1024)
#endif

#ifndef d_m3SkipMemoryBoundsCheck
#  define d_m3SkipMemoryBoundsCheck            0       // skip memory bounds checks
#endif

// Back each linear memory with enough reserved address space that every address a
// Wasm instruction could name lands inside it, and commit only the part the memory
// actually has. An access past the end then reaches address space that is reserved
// and not committed, and the fault that follows is turned back into the trap the
// spec asks for - so the bounds check on every load and store goes away entirely.
//
// It asks a lot of the system, and m3_config_platforms.h turns it on for every target
// that answers - which leaves it off here for everything else:
//
//   - 64-bit only. The reservation is 8GiB per memory, because a 32-bit address
//     plus a 32-bit memarg offset reaches that far; there is no room for that in a
//     32-bit address space, and none to spare on anything embedded.
//   - address space to reserve, and a way to say so: mmap on POSIX, VirtualAlloc on
//     Windows. m3_host_none.h refuses to build with this on.
//   - a way to catch the fault. On Windows that means a compiler that speaks
//     __try/__except - MSVC or clang-cl, not MinGW.
//   - no d_m3FixedHeap: the memory comes from the system here, not from that heap.
//
// d_m3GuardedArenaSlots caps how many memories a process can have at once, since
// each one takes a slot of that reserved arena. It costs address space and nothing
// else - no memory is committed for a slot until a module asks for one. The arena is
// shared by every runtime in the process and is not itself locked, so two threads
// loading modules at the same time need the embedder to keep them apart, as the rest
// of loading already does.
#ifndef d_m3GuardedMemory
#  define d_m3GuardedMemory                    0
#endif

#ifndef d_m3GuardedArenaSlots
#  define d_m3GuardedArenaSlots                128
#endif

// What it is incompatible with is checked in m3_host.h, which is the first place
// that has seen enough of the configuration to say

#define d_m3EnableCodePageRefCounting          0       // not supported currently

#endif // m3_config_h
