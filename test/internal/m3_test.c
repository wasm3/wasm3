//
//  m3_test.c
//
//  Created by Steven Massey on 2/27/20.
//  Copyright © 2020 Steven Massey. All rights reserved.
//

#include <stdio.h>

#include "wasm3_ext.h"
#include "m3_bind.h"
#include "m3_host.h"
#include "m3_env.h"
#include "m3_deterministic.h"

#if defined(d_m3HasWASI) || defined(d_m3HasMetaWASI) || defined(d_m3HasUVWASI)
#  include "m3_api_wasi.h"
#  define d_m3TestLinksWASI 1
#endif

// Whether this build can run a case on a thread of its own, which the native stack
// tests below need: a stack budget is only interesting against a stack that is not
// the one the process started on. Win32 always can; elsewhere it takes pthreads,
// which CMake says whether it found.
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  define d_m3TestHasThreads 1
#elif defined(d_m3TestHasPthreads)
#  include <pthread.h>
#  define d_m3TestHasThreads 1
#else
#  define d_m3TestHasThreads 0
#endif

static int failures = 0;

#define Test(NAME) if (RunTest (argc, argv, #NAME) != 0)
#define DisabledTest(NAME) printf ("\ndisabled: %s\n", #NAME); if (false)

// Deliberately a bare 'if' rather than a do/while: it is used both with and
// without a trailing semicolon below, and only one of those survives the latter.
#define expect(TEST) if (not (TEST)) { printf ("failed: (%s) on line: %d\n", #TEST, __LINE__); ++failures; }


// (module
//   (import "env" "cb" (func $cb))
//   (memory 1)
//   (func (export "outer") (call $cb))
//   (func (export "oob") (result i32) (i32.load (i32.const 0xFFFF0000))))
//
// "outer" calls the host, which calls "oob" back, which reads far past the one page
// this memory has - see the case that uses it.
static const u8 c_reenterWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x60, 0x00, 0x00, 0x60,
    0x00, 0x01, 0x7f, 0x02, 0x0a, 0x01, 0x03, 0x65, 0x6e, 0x76, 0x02, 0x63, 0x62, 0x00, 0x00,
    0x03, 0x03, 0x02, 0x00, 0x01, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07, 0x0f, 0x02, 0x05, 0x6f,
    0x75, 0x74, 0x65, 0x72, 0x00, 0x01, 0x03, 0x6f, 0x6f, 0x62, 0x00, 0x02, 0x0a, 0x10, 0x02,
    0x04, 0x00, 0x10, 0x00, 0x0b, 0x09, 0x00, 0x41, 0x80, 0x80, 0x7c, 0x28, 0x02, 0x00, 0x0b
};

// (module
//   (memory 1)
//   (func (export "size") (result i32) (memory.size))
//   (func (export "grow") (result i32) (memory.grow (i32.const 1)))
//   (func (export "load") (param i32) (result i32) (i32.load8_u (local.get 0))))
//
// One declared page, so a byte limit that falls inside it has something to cap.
static const u8 c_memoryPage[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x02, 0x60, 0x00, 0x01, 0x7f,
    0x60, 0x01, 0x7f, 0x01, 0x7f, 0x03, 0x04, 0x03, 0x00, 0x00, 0x01, 0x05, 0x03, 0x01, 0x00,
    0x01, 0x07, 0x16, 0x03, 0x04, 0x73, 0x69, 0x7a, 0x65, 0x00, 0x00, 0x04, 0x67, 0x72, 0x6f,
    0x77, 0x00, 0x01, 0x04, 0x6c, 0x6f, 0x61, 0x64, 0x00, 0x02, 0x0a, 0x15, 0x03, 0x04, 0x00,
    0x3f, 0x00, 0x0b, 0x06, 0x00, 0x41, 0x01, 0x40, 0x00, 0x0b, 0x07, 0x00, 0x20, 0x00, 0x2d,
    0x00, 0x00, 0x0b
};

// (module
//   (memory $m 1) (export "m1" (memory $m)) (export "m2" (memory $m))
//   (table $t 1 funcref) (export "t1" (table $t)) (export "t2" (table $t))
//   (global $g i32 (i32.const 42)) (export "g1" (global $g)) (export "g2" (global $g))
//   (func $f (result i32) (i32.const 5))
//   (export "f1" (func $f)) (export "f2" (func $f)) (export "f3" (func $f)) (export "f4" (func $f)))
//
// Everything it has exported under more than one name, the function under more
// names than an M3Function keeps for itself. Loaded as "owner" by the link cases.
static const u8 c_linkOwnerWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00, 0x04, 0x04, 0x01, 0x70, 0x00, 0x01, 0x05, 0x03, 0x01, 0x00, 0x01,
    0x06, 0x06, 0x01, 0x7f, 0x00, 0x41, 0x2a, 0x0b, 0x07, 0x33, 0x0a, 0x02, 0x6d, 0x31, 0x02,
    0x00, 0x02, 0x6d, 0x32, 0x02, 0x00, 0x02, 0x74, 0x31, 0x01, 0x00, 0x02, 0x74, 0x32, 0x01,
    0x00, 0x02, 0x67, 0x31, 0x03, 0x00, 0x02, 0x67, 0x32, 0x03, 0x00, 0x02, 0x66, 0x31, 0x00,
    0x00, 0x02, 0x66, 0x32, 0x00, 0x00, 0x02, 0x66, 0x33, 0x00, 0x00, 0x02, 0x66, 0x34, 0x00,
    0x00, 0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x05, 0x0b
};

#if d_m3HasStackSwitching

// The shape the whole feature rests on: create a continuation, resume it,
// take the value it suspends with, resume it again, and see it run to the end.
static const u8 c_ssBasicWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x03,
    0x02, 0x00, 0x04, 0x0d, 0x03, 0x01, 0x00, 0x02,
    0x07, 0x08, 0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e,
    0x00, 0x01, 0x09, 0x05, 0x01, 0x03, 0x00, 0x01,
    0x00, 0x0a, 0x31, 0x02, 0x06, 0x00, 0x41, 0x2a,
    0xe2, 0x00, 0x0b, 0x28, 0x02, 0x01, 0x63, 0x01,
    0x01, 0x7f, 0xd2, 0x00, 0xe0, 0x01, 0x21, 0x00,
    0x03, 0x40, 0x02, 0x03, 0x20, 0x00, 0xe3, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x20, 0x01, 0x0f, 0x0b,
    0x21, 0x00, 0x20, 0x01, 0x6a, 0x21, 0x01, 0x0c,
    0x00, 0x0b, 0x00, 0x0b
};

// Continuations are one-shot: the second resume has to trap.
static const u8 c_ssOneShotWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x0a, 0x03, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x00, 0x01, 0x7f, 0x03, 0x03, 0x02, 0x00,
    0x02, 0x07, 0x08, 0x01, 0x04, 0x6d, 0x61, 0x69,
    0x6e, 0x00, 0x01, 0x09, 0x05, 0x01, 0x03, 0x00,
    0x01, 0x00, 0x0a, 0x1a, 0x02, 0x02, 0x00, 0x0b,
    0x15, 0x01, 0x01, 0x63, 0x01, 0xd2, 0x00, 0xe0,
    0x01, 0x22, 0x00, 0xe3, 0x01, 0x00, 0x20, 0x00,
    0xe3, 0x01, 0x00, 0x41, 0x00, 0x0b
};

// cont.bind: the first argument is bound now, the second at the resume.
static const u8 c_ssBindWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x02, 0x7f, 0x7f, 0x01,
    0x7f, 0x5d, 0x00, 0x60, 0x01, 0x7f, 0x01, 0x7f,
    0x5d, 0x02, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x03,
    0x02, 0x00, 0x04, 0x07, 0x08, 0x01, 0x04, 0x6d,
    0x61, 0x69, 0x6e, 0x00, 0x01, 0x09, 0x05, 0x01,
    0x03, 0x00, 0x01, 0x00, 0x0a, 0x21, 0x02, 0x07,
    0x00, 0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b, 0x17,
    0x01, 0x01, 0x63, 0x03, 0x41, 0x0a, 0xd2, 0x00,
    0xe0, 0x01, 0xe1, 0x01, 0x03, 0x21, 0x00, 0x41,
    0x20, 0x20, 0x00, 0xe3, 0x03, 0x00, 0x0b
};

// A continuation that suspends from inside two nested loops. op_Loop keeps a
// native frame and recognises a back edge by the pc handed back to it; both of
// those frames are gone once the suspend has unwound, so resuming has to build
// them again or the inner loop's next back edge lands nowhere.
static const u8 c_ssNestedLoopsWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x03,
    0x02, 0x00, 0x04, 0x0d, 0x03, 0x01, 0x00, 0x02,
    0x07, 0x08, 0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e,
    0x00, 0x01, 0x09, 0x05, 0x01, 0x03, 0x00, 0x01,
    0x00, 0x0a, 0x53, 0x02, 0x28, 0x01, 0x02, 0x7f,
    0x41, 0x02, 0x21, 0x01, 0x03, 0x40, 0x41, 0x03,
    0x21, 0x00, 0x03, 0x40, 0x20, 0x00, 0xe2, 0x00,
    0x20, 0x00, 0x41, 0x01, 0x6b, 0x22, 0x00, 0x0d,
    0x00, 0x0b, 0x20, 0x01, 0x41, 0x01, 0x6b, 0x22,
    0x01, 0x0d, 0x00, 0x0b, 0x0b, 0x28, 0x02, 0x01,
    0x63, 0x01, 0x01, 0x7f, 0xd2, 0x00, 0xe0, 0x01,
    0x21, 0x00, 0x03, 0x40, 0x02, 0x03, 0x20, 0x00,
    0xe3, 0x01, 0x01, 0x00, 0x00, 0x00, 0x20, 0x01,
    0x0f, 0x0b, 0x21, 0x00, 0x20, 0x01, 0x6a, 0x21,
    0x01, 0x0c, 0x00, 0x0b, 0x00, 0x0b
};

// The suspend happens a call deep, inside a loop: the recorded frames have to
// interleave the call and the loop in the order the unwind met them.
static const u8 c_ssCallInLoopWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x04,
    0x03, 0x00, 0x04, 0x02, 0x0d, 0x03, 0x01, 0x00,
    0x02, 0x07, 0x08, 0x01, 0x04, 0x6d, 0x61, 0x69,
    0x6e, 0x00, 0x01, 0x09, 0x05, 0x01, 0x03, 0x00,
    0x01, 0x00, 0x0a, 0x4a, 0x03, 0x18, 0x01, 0x01,
    0x7f, 0x41, 0x03, 0x21, 0x00, 0x03, 0x40, 0x20,
    0x00, 0x10, 0x02, 0x20, 0x00, 0x41, 0x01, 0x6b,
    0x22, 0x00, 0x0d, 0x00, 0x0b, 0x0b, 0x28, 0x02,
    0x01, 0x63, 0x01, 0x01, 0x7f, 0xd2, 0x00, 0xe0,
    0x01, 0x21, 0x00, 0x03, 0x40, 0x02, 0x03, 0x20,
    0x00, 0xe3, 0x01, 0x01, 0x00, 0x00, 0x00, 0x20,
    0x01, 0x0f, 0x0b, 0x21, 0x00, 0x20, 0x01, 0x6a,
    0x21, 0x01, 0x0c, 0x00, 0x0b, 0x00, 0x0b, 0x06,
    0x00, 0x20, 0x00, 0xe2, 0x00, 0x0b
};

// loop -> call -> loop -> suspend, driven twice round the outer loop, so the
// frames are rebuilt and then torn down again by a second suspend from inside
// the chain the first resume had just replayed.
static const u8 c_ssLoopCallLoopWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x04,
    0x03, 0x00, 0x04, 0x00, 0x0d, 0x03, 0x01, 0x00,
    0x02, 0x07, 0x08, 0x01, 0x04, 0x6d, 0x61, 0x69,
    0x6e, 0x00, 0x01, 0x09, 0x05, 0x01, 0x03, 0x00,
    0x01, 0x00, 0x0a, 0x5a, 0x03, 0x16, 0x01, 0x01,
    0x7f, 0x41, 0x02, 0x21, 0x00, 0x03, 0x40, 0x10,
    0x02, 0x20, 0x00, 0x41, 0x01, 0x6b, 0x22, 0x00,
    0x0d, 0x00, 0x0b, 0x0b, 0x28, 0x02, 0x01, 0x63,
    0x01, 0x01, 0x7f, 0xd2, 0x00, 0xe0, 0x01, 0x21,
    0x00, 0x03, 0x40, 0x02, 0x03, 0x20, 0x00, 0xe3,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x20, 0x01, 0x0f,
    0x0b, 0x21, 0x00, 0x20, 0x01, 0x6a, 0x21, 0x01,
    0x0c, 0x00, 0x0b, 0x00, 0x0b, 0x18, 0x01, 0x01,
    0x7f, 0x41, 0x03, 0x21, 0x00, 0x03, 0x40, 0x20,
    0x00, 0xe2, 0x00, 0x20, 0x00, 0x41, 0x01, 0x6b,
    0x22, 0x00, 0x0d, 0x00, 0x0b, 0x0b
};

// A continuation suspends inside a try region and throws after it has been
// resumed. op_TryTable's frame is what catches, so the replay has to stand it
// back up - and restore the handler depth it was holding - or the exception
// escapes the continuation entirely.
#  if d_m3HasExceptionHandling
static const u8 c_ssSuspendInTryWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x03,
    0x02, 0x00, 0x04, 0x0d, 0x05, 0x02, 0x00, 0x02,
    0x00, 0x00, 0x07, 0x08, 0x01, 0x04, 0x6d, 0x61,
    0x69, 0x6e, 0x00, 0x01, 0x09, 0x05, 0x01, 0x03,
    0x00, 0x01, 0x00, 0x0a, 0x3d, 0x02, 0x12, 0x00,
    0x02, 0x40, 0x1f, 0x40, 0x01, 0x00, 0x01, 0x00,
    0x41, 0x07, 0xe2, 0x00, 0x08, 0x01, 0x0b, 0x0b,
    0x0b, 0x28, 0x02, 0x01, 0x63, 0x01, 0x01, 0x7f,
    0xd2, 0x00, 0xe0, 0x01, 0x21, 0x00, 0x03, 0x40,
    0x02, 0x03, 0x20, 0x00, 0xe3, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x20, 0x01, 0x0f, 0x0b, 0x21, 0x00,
    0x20, 0x01, 0x6a, 0x21, 0x01, 0x0c, 0x00, 0x0b,
    0x00, 0x0b
};
#  endif

// The continuation grows linear memory between suspends. Growing moves the
// memory header, so a rebuilt frame has to re-derive _mem from the M3Memory
// rather than from a header pointer captured when the frame was recorded.
static const u8 c_ssGrowInContinuationWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x03,
    0x02, 0x00, 0x04, 0x05, 0x03, 0x01, 0x00, 0x01,
    0x0d, 0x03, 0x01, 0x00, 0x02, 0x07, 0x08, 0x01,
    0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x01, 0x09,
    0x05, 0x01, 0x03, 0x00, 0x01, 0x00, 0x0a, 0x59,
    0x02, 0x26, 0x01, 0x01, 0x7f, 0x41, 0x03, 0x21,
    0x00, 0x03, 0x40, 0x20, 0x00, 0xe2, 0x00, 0x41,
    0x01, 0x40, 0x00, 0x1a, 0x41, 0x80, 0x80, 0x04,
    0x41, 0x07, 0x36, 0x02, 0x00, 0x20, 0x00, 0x41,
    0x01, 0x6b, 0x22, 0x00, 0x0d, 0x00, 0x0b, 0x0b,
    0x30, 0x02, 0x01, 0x63, 0x01, 0x01, 0x7f, 0xd2,
    0x00, 0xe0, 0x01, 0x21, 0x00, 0x03, 0x40, 0x02,
    0x03, 0x20, 0x00, 0xe3, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x20, 0x01, 0x41, 0x80, 0x80, 0x04, 0x28,
    0x02, 0x00, 0x6a, 0x0f, 0x0b, 0x21, 0x00, 0x20,
    0x01, 0x6a, 0x21, 0x01, 0x0c, 0x00, 0x0b, 0x00,
    0x0b
};

// Suspending from further down than d_m3ContinuationMaxFrames can record.
// The suspend cannot be resumed, so it has to become a trap rather than a
// continuation with a truncated frame list.
static const u8 c_ssFrameOverflowWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x18, 0x06, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x60, 0x01,
    0x7f, 0x00, 0x03, 0x04, 0x03, 0x00, 0x04, 0x02,
    0x0d, 0x03, 0x01, 0x00, 0x02, 0x07, 0x08, 0x01,
    0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x01, 0x09,
    0x05, 0x01, 0x03, 0x00, 0x01, 0x00, 0x0a, 0x4b,
    0x03, 0x07, 0x00, 0x41, 0x90, 0x03, 0x10, 0x02,
    0x0b, 0x28, 0x02, 0x01, 0x63, 0x01, 0x01, 0x7f,
    0xd2, 0x00, 0xe0, 0x01, 0x21, 0x00, 0x03, 0x40,
    0x02, 0x03, 0x20, 0x00, 0xe3, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x20, 0x01, 0x0f, 0x0b, 0x21, 0x00,
    0x20, 0x01, 0x6a, 0x21, 0x01, 0x0c, 0x00, 0x0b,
    0x00, 0x0b, 0x18, 0x00, 0x02, 0x40, 0x20, 0x00,
    0x45, 0x04, 0x40, 0x41, 0x01, 0xe2, 0x00, 0x0c,
    0x01, 0x0b, 0x20, 0x00, 0x41, 0x01, 0x6b, 0x10,
    0x02, 0x0b, 0x0b
};

// A suspend whose tag is named by a resume further out than the one it first
// meets. What the handler receives is everything in between - the resume it
// travelled through, and the continuation under that - so resuming it runs the
// inner continuation out, lets the intervening resume finish, and carries its
// continuation on to its own end.
//
//   inner:  suspends with $outer, then writes 4
//   middle: writes 1, resumes inner under a handler for $inner alone, writes 5
//   main:   resumes middle under a handler for $outer, writes the payload 7,
//           then resumes what it was handed             -> 1, 7, 4, 5
static const u8 c_ssNestedPromptWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x04,
    0x03, 0x00, 0x00, 0x04, 0x0d, 0x05, 0x02, 0x00,
    0x02, 0x00, 0x02, 0x06, 0x06, 0x01, 0x7f, 0x01,
    0x41, 0x00, 0x0b, 0x07, 0x08, 0x01, 0x04, 0x6d,
    0x61, 0x69, 0x6e, 0x00, 0x02, 0x09, 0x06, 0x01,
    0x03, 0x00, 0x02, 0x00, 0x01, 0x0a, 0x62, 0x03,
    0x10, 0x00, 0x41, 0x07, 0xe2, 0x00, 0x23, 0x00,
    0x41, 0x0a, 0x6c, 0x41, 0x04, 0x6a, 0x24, 0x00,
    0x0b, 0x2a, 0x00, 0x23, 0x00, 0x41, 0x0a, 0x6c,
    0x41, 0x01, 0x6a, 0x24, 0x00, 0x02, 0x40, 0x02,
    0x03, 0xd2, 0x00, 0xe0, 0x01, 0xe3, 0x01, 0x01,
    0x00, 0x01, 0x00, 0x0c, 0x01, 0x0b, 0x1a, 0x1a,
    0x0b, 0x23, 0x00, 0x41, 0x0a, 0x6c, 0x41, 0x05,
    0x6a, 0x24, 0x00, 0x0b, 0x24, 0x01, 0x01, 0x63,
    0x01, 0x02, 0x03, 0xd2, 0x01, 0xe0, 0x01, 0xe3,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x21,
    0x00, 0x23, 0x00, 0x41, 0x0a, 0x6c, 0x6a, 0x24,
    0x00, 0x20, 0x00, 0xe3, 0x01, 0x00, 0x23, 0x00,
    0x0b
};

// The same, with two resumes in between rather than one.
//
//   -> 1, 2, 7, 4, 6, 5
static const u8 c_ssNestedPrompt2Wasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x05,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x0d, 0x07, 0x03,
    0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x06, 0x06,
    0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x07, 0x08,
    0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x03,
    0x09, 0x07, 0x01, 0x03, 0x00, 0x03, 0x00, 0x01,
    0x02, 0x0a, 0x8d, 0x01, 0x04, 0x10, 0x00, 0x41,
    0x07, 0xe2, 0x00, 0x23, 0x00, 0x41, 0x0a, 0x6c,
    0x41, 0x04, 0x6a, 0x24, 0x00, 0x0b, 0x2a, 0x00,
    0x23, 0x00, 0x41, 0x0a, 0x6c, 0x41, 0x02, 0x6a,
    0x24, 0x00, 0x02, 0x40, 0x02, 0x03, 0xd2, 0x00,
    0xe0, 0x01, 0xe3, 0x01, 0x01, 0x00, 0x02, 0x00,
    0x0c, 0x01, 0x0b, 0x1a, 0x1a, 0x0b, 0x23, 0x00,
    0x41, 0x0a, 0x6c, 0x41, 0x06, 0x6a, 0x24, 0x00,
    0x0b, 0x2a, 0x00, 0x23, 0x00, 0x41, 0x0a, 0x6c,
    0x41, 0x01, 0x6a, 0x24, 0x00, 0x02, 0x40, 0x02,
    0x03, 0xd2, 0x01, 0xe0, 0x01, 0xe3, 0x01, 0x01,
    0x00, 0x01, 0x00, 0x0c, 0x01, 0x0b, 0x1a, 0x1a,
    0x0b, 0x23, 0x00, 0x41, 0x0a, 0x6c, 0x41, 0x05,
    0x6a, 0x24, 0x00, 0x0b, 0x24, 0x01, 0x01, 0x63,
    0x01, 0x02, 0x03, 0xd2, 0x02, 0xe0, 0x01, 0xe3,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x21,
    0x00, 0x23, 0x00, 0x41, 0x0a, 0x6c, 0x6a, 0x24,
    0x00, 0x20, 0x00, 0xe3, 0x01, 0x00, 0x23, 0x00,
    0x0b
};

// The proposal's scheduler2 shape, which needs a recursive continuation type:
//
//     (rec (type $ft (func (param (ref null $ct))))
//          (type $ct (cont $ft)))
//
// Two tasks switch straight to each other, each handed the peer that switched
// to it - which is what the recursion is for. The entry hands the first task a
// null peer, spelled ref.null nocont.
static const u8 c_ssScheduler2Wasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x11, 0x03, 0x4e, 0x02, 0x60, 0x01, 0x63,
    0x01, 0x00, 0x5d, 0x00, 0x60, 0x00, 0x00, 0x60,
    0x00, 0x01, 0x7f, 0x03, 0x04, 0x03, 0x00, 0x00,
    0x03, 0x0d, 0x03, 0x01, 0x00, 0x02, 0x06, 0x06,
    0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x07, 0x08,
    0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x02,
    0x09, 0x06, 0x01, 0x03, 0x00, 0x02, 0x00, 0x01,
    0x0a, 0x47, 0x03, 0x18, 0x00, 0x41, 0x01, 0x24,
    0x00, 0xd2, 0x01, 0xe0, 0x01, 0xe6, 0x01, 0x00,
    0x1a, 0x23, 0x00, 0x41, 0x0a, 0x6c, 0x41, 0x03,
    0x6a, 0x24, 0x00, 0x0b, 0x1c, 0x00, 0x23, 0x00,
    0x41, 0x0a, 0x6c, 0x41, 0x02, 0x6a, 0x24, 0x00,
    0x20, 0x00, 0xe6, 0x01, 0x00, 0x1a, 0x23, 0x00,
    0x41, 0x0a, 0x6c, 0x41, 0x04, 0x6a, 0x24, 0x00,
    0x0b, 0x0f, 0x00, 0xd0, 0x75, 0xd2, 0x00, 0xe0,
    0x01, 0xe3, 0x01, 0x01, 0x01, 0x00, 0x23, 0x00,
    0x0b
};

// Symmetric switching. $ct1's trailing parameter names $ct0, so switch hands
// the peer the continuation it just suspended, and the resume that installed
// (on $yield switch) carries on tracking whichever one is running under it.
//
//   zero:  writes 1, switches to a fresh one, and on the way back writes 3
//   one:   writes 2, resumes the continuation it was handed, then writes 4
static const u8 c_ssSwitchWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x11, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x63, 0x01, 0x00, 0x5d, 0x02, 0x60,
    0x00, 0x01, 0x7f, 0x03, 0x04, 0x03, 0x00, 0x02,
    0x04, 0x0d, 0x03, 0x01, 0x00, 0x00, 0x06, 0x06,
    0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x07, 0x08,
    0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x02,
    0x09, 0x06, 0x01, 0x03, 0x00, 0x02, 0x00, 0x01,
    0x0a, 0x43, 0x03, 0x17, 0x00, 0x41, 0x01, 0x24,
    0x00, 0xd2, 0x01, 0xe0, 0x03, 0xe6, 0x03, 0x00,
    0x23, 0x00, 0x41, 0x0a, 0x6c, 0x41, 0x03, 0x6a,
    0x24, 0x00, 0x0b, 0x1b, 0x00, 0x23, 0x00, 0x41,
    0x0a, 0x6c, 0x41, 0x02, 0x6a, 0x24, 0x00, 0x20,
    0x00, 0xe3, 0x01, 0x00, 0x23, 0x00, 0x41, 0x0a,
    0x6c, 0x41, 0x04, 0x6a, 0x24, 0x00, 0x0b, 0x0d,
    0x00, 0xd2, 0x00, 0xe0, 0x01, 0xe3, 0x01, 0x01,
    0x01, 0x00, 0x23, 0x00, 0x0b
};

#  if d_m3HasExceptionHandling
// resume_throw raises its exception at the continuation's suspension point.
// This one has no handler of its own, so the abort travels out through the
// frames the resume had just rebuilt and lands in the caller's try_table.
static const u8 c_ssResumeThrowWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x03,
    0x02, 0x00, 0x04, 0x0d, 0x05, 0x02, 0x00, 0x02,
    0x00, 0x00, 0x07, 0x08, 0x01, 0x04, 0x6d, 0x61,
    0x69, 0x6e, 0x00, 0x01, 0x09, 0x05, 0x01, 0x03,
    0x00, 0x01, 0x00, 0x0a, 0x3a, 0x02, 0x07, 0x00,
    0x41, 0x07, 0xe2, 0x00, 0x00, 0x0b, 0x30, 0x02,
    0x01, 0x63, 0x01, 0x01, 0x7f, 0x02, 0x03, 0xd2,
    0x00, 0xe0, 0x01, 0xe3, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x21, 0x00, 0x21, 0x01, 0x02,
    0x40, 0x1f, 0x40, 0x01, 0x00, 0x01, 0x00, 0x20,
    0x00, 0xe4, 0x01, 0x01, 0x00, 0x00, 0x0b, 0x0b,
    0x20, 0x01, 0x41, 0xe4, 0x00, 0x6a, 0x0b
};

// The continuation catches the abort itself, in a try region that only exists
// again because the replay rebuilt it, and then returns normally - so the
// resume_throw completes rather than propagating.
static const u8 c_ssResumeThrowCaughtWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x14, 0x05, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x03,
    0x02, 0x00, 0x04, 0x0d, 0x05, 0x02, 0x00, 0x02,
    0x00, 0x00, 0x06, 0x06, 0x01, 0x7f, 0x01, 0x41,
    0x00, 0x0b, 0x07, 0x08, 0x01, 0x04, 0x6d, 0x61,
    0x69, 0x6e, 0x00, 0x01, 0x09, 0x05, 0x01, 0x03,
    0x00, 0x01, 0x00, 0x0a, 0x3a, 0x02, 0x18, 0x00,
    0x02, 0x40, 0x1f, 0x40, 0x01, 0x00, 0x01, 0x00,
    0x41, 0x07, 0xe2, 0x00, 0x00, 0x0b, 0x0b, 0x23,
    0x00, 0x41, 0x32, 0x6a, 0x24, 0x00, 0x0b, 0x1f,
    0x01, 0x01, 0x63, 0x01, 0x02, 0x03, 0xd2, 0x00,
    0xe0, 0x01, 0xe3, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x0b, 0x21, 0x00, 0x24, 0x00, 0x20, 0x00,
    0xe4, 0x01, 0x01, 0x00, 0x23, 0x00, 0x0b
};

// Aborting a continuation that never started. There is no suspension point
// to raise at, so the exception has to come straight back out of the resume.
static const u8 c_ssResumeThrowFreshWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x0a, 0x03, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x00, 0x01, 0x7f, 0x03, 0x03, 0x02, 0x00,
    0x02, 0x0d, 0x03, 0x01, 0x00, 0x00, 0x07, 0x08,
    0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x01,
    0x09, 0x05, 0x01, 0x03, 0x00, 0x01, 0x00, 0x0a,
    0x1d, 0x02, 0x02, 0x00, 0x0b, 0x18, 0x00, 0x02,
    0x40, 0x1f, 0x40, 0x01, 0x00, 0x00, 0x00, 0xd2,
    0x00, 0xe0, 0x01, 0xe4, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x0b, 0x41, 0x09, 0x0b
};

// resume_throw_ref, with an exnref caught here and thrown into the
// continuation rather than a fresh exception built from a tag.
static const u8 c_ssResumeThrowRefWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x18, 0x06, 0x60, 0x00, 0x00, 0x5d, 0x00,
    0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x02, 0x7f,
    0x63, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x60, 0x00,
    0x01, 0x69, 0x03, 0x03, 0x02, 0x00, 0x04, 0x0d,
    0x05, 0x02, 0x00, 0x02, 0x00, 0x00, 0x06, 0x06,
    0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x07, 0x08,
    0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x01,
    0x09, 0x05, 0x01, 0x03, 0x00, 0x01, 0x00, 0x0a,
    0x4c, 0x02, 0x18, 0x00, 0x02, 0x40, 0x1f, 0x40,
    0x01, 0x00, 0x01, 0x00, 0x41, 0x07, 0xe2, 0x00,
    0x00, 0x0b, 0x0b, 0x23, 0x00, 0x41, 0x32, 0x6a,
    0x24, 0x00, 0x0b, 0x31, 0x02, 0x01, 0x63, 0x01,
    0x01, 0x69, 0x02, 0x03, 0xd2, 0x00, 0xe0, 0x01,
    0xe3, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0b,
    0x21, 0x00, 0x24, 0x00, 0x02, 0x05, 0x1f, 0x40,
    0x01, 0x01, 0x01, 0x00, 0x08, 0x01, 0x0b, 0x00,
    0x0b, 0x21, 0x01, 0x20, 0x01, 0x20, 0x00, 0xe5,
    0x01, 0x00, 0x23, 0x00, 0x0b
};
#  endif

#endif


#if d_m3HasSnapshots

// The embedder side of a snapshot: an externref goes out as 42 and comes back
// as whatever the loading runtime says 42 is, and the host's own state is a
// string
static void* g_snapshotTestBefore;
static void* g_snapshotTestAfter;
static char  g_snapshotTestHostState[32];
static u32   g_snapshotTestBindings;
static u32   g_snapshotTestHostLoads;
static bool  g_snapshotTestReadPastEnd;
static bool  g_snapshotTestReadShort;

static
M3Result SnapshotTest_NameExternRef (void* i_userdata, void* i_reference, uint64_t* o_name)
{
    (void)i_userdata;
    *o_name = 42;
    return (i_reference == g_snapshotTestBefore) ? m3Err_none : "an externref the host did not make";
}

static
M3Result SnapshotTest_BindExternRef (void* i_userdata, uint64_t i_name, void** o_reference)
{
    (void)i_userdata;
    g_snapshotTestBindings++;
    *o_reference = g_snapshotTestAfter;
    return (i_name == 42) ? m3Err_none : "an externref the host never named";
}

static
M3Result SnapshotTest_SaveHostState (void* i_userdata, M3SnapshotWriter i_writer, void* i_writerData)
{
    static const char c_state[] = "the host's own";

    (void)i_userdata;
    return i_writer(c_state, sizeof(c_state), i_writerData);
}

static
M3Result SnapshotTest_LoadHostState (void* i_userdata, M3SnapshotReader i_reader, void* i_readerData, size_t i_size)
{
    (void)i_userdata;
    g_snapshotTestHostLoads++;

    if (i_size > sizeof(g_snapshotTestHostState)) {
        return "more host state than was saved";
    }

    // a host that takes less than it was given leaves the rest unread
    M3Result result = i_reader(g_snapshotTestHostState, i_size - (g_snapshotTestReadShort ? 1 : 0), i_readerData);
    if (g_snapshotTestReadPastEnd) {
        char extra;
        // Even a callback that ignores the error cannot consume another section.
        (void)i_reader(&extra, 1, i_readerData);
    }
    return result;
}

// Add trailing junk inside one short section, leaving subsequent framing valid.
static
void* SnapshotTest_AppendSectionByte (const void* i_bytes, size_t i_size, u8 i_id)
{
    bytes_t pos = (bytes_t)i_bytes + 8;
    bytes_t end = (bytes_t)i_bytes + i_size;
    while (pos < end) {
        u8      id     = *pos++;
        bytes_t length = pos;
        u32     size   = 0;
        if (ReadLEB_u32(&size, &pos, end) or size > (size_t)(end - pos)) {
            return NULL;
        }
        if (id == i_id and pos == length + 1 and size < 127) {
            size_t offset = (size_t)(pos + size - (bytes_t)i_bytes);
            u8*    copy   = (u8*)malloc(i_size + 1);
            if (copy) {
                memcpy(copy, i_bytes, offset);
                copy[length - (bytes_t)i_bytes]++;
                copy[offset] = 0;
                memcpy(copy + offset + 1, pos + size, i_size - offset);
            }
            return copy;
        }
        pos += size;
    }
    return NULL;
}

#endif


#if d_m3HasSnapshots && d_m3HasGasMetering

// A runtime of its own, in an environment of its own: a program carried across
// thousands of these would otherwise fill one environment's type table
typedef struct RoundTripLeg {
    IM3Environment env;
    IM3Runtime     runtime;
} RoundTripLeg;

static
void EndRoundTripLeg (RoundTripLeg* io_leg)
{
    if (io_leg->runtime) {
        m3_FreeRuntime(io_leg->runtime);
    }
    if (io_leg->env) {
        m3_FreeEnvironment(io_leg->env);
    }

    io_leg->runtime = NULL;
    io_leg->env     = NULL;
}

// Runs "main" to its end from a snapshot, in a runtime that is not metering
static
M3Result FinishUnmetered (const u8* i_wasm, u32 i_size, const void* i_snapshot, size_t i_snapshotSize,
                          i32* o_result)
{
    M3Result     result   = m3Err_none;
    RoundTripLeg leg      = { NULL, NULL };
    IM3Module    module   = NULL;
    IM3Function  function = NULL;

    *o_result = 0;

    leg.env     = m3_NewEnvironment();
    leg.runtime = leg.env ? m3_NewRuntime(leg.env, 64 * 1024, NULL) : NULL;

    if (not leg.runtime) {
        result = m3Err_mallocFailed;
    }
    if (not result) {
        m3_SetSuspendable(leg.runtime, true);
        result = m3_ParseModule(leg.env, &module, i_wasm, i_size);
    }
    if (not result) {
        result = m3_LoadModule(leg.runtime, module);
    }
    if (not result) {
        result = m3_FindFunction(&function, leg.runtime, "main");
    }
    if (not result) {
        result = m3_LoadSnapshotFromBuffer(leg.runtime, module, i_snapshot, i_snapshotSize);
    }
    if (not result) {
        result = m3_ResumeRuntime(leg.runtime);
    }
    if (not result) {
        result = m3_GetResultsV(function, o_result);
    }

    EndRoundTripLeg(&leg);

    return result;
}

// Runs "main" to its end in as many runtimes as it takes: whenever it pauses,
// the program is saved, its runtime is thrown away, and a new one takes over
// from the snapshot. The result can only come out right if every one of those
// pauses came back whole.
//
// With i_step, every leg is asked to pause before it starts, so the program
// stops at every back edge and function entry it passes - each resume goes on
// past the point it stopped at. Without it, the legs pause because their gas
// runs out, and with i_finishUnmetered, every such pause is also taken to the
// end in a runtime that is not metering, and has to come out at i_expected
// there too.
//
// Every leg meters, so it can tell whether it went anywhere: a resume that stops
// where it started has spent no gas, and fails the run rather than hanging it.
//
// Each runtime is kept until the next one has run, so nothing the new one
// allocates can land where a pointer the snapshot failed to translate would
// still happen to work.
static
M3Result RunInRoundTrips (const u8* i_wasm, u32 i_size, i32* o_result, u32* o_numStops, bool i_step,
                          bool i_finishUnmetered, i32 i_expected)
{
    M3Result     result    = m3Err_none;
    RoundTripLeg leg       = { NULL, NULL };
    RoundTripLeg retired   = { NULL, NULL };
    IM3Function  function  = NULL;
    void*        saved     = NULL;
    size_t       savedSize = 0;
    // one gas unit, clear of rounding down to none - or, when stepping, more
    // than any of these programs spends
    double budget = i_step ? 1e9 : 0.00015;

    *o_numStops = 0;

    for (;;) {
        IM3Module module = NULL;

        leg.env     = m3_NewEnvironment();
        leg.runtime = leg.env ? m3_NewRuntime(leg.env, 64 * 1024, NULL) : NULL;
        if (not leg.runtime) {
            result = m3Err_mallocFailed;
            break;
        }

        // both before anything compiles
        m3_SetSuspendable(leg.runtime, true);
        m3_SetResourceLimit(leg.runtime, c_m3Limit_GasUnits, (uint64_t)((budget)*M3_GAS_UNITS_PER_GAS));

        result = m3_ParseModule(leg.env, &module, i_wasm, i_size);
        if (result) {
            break;
        }
        result = m3_LoadModule(leg.runtime, module);
        if (result) {
            break;
        }
        result = m3_FindFunction(&function, leg.runtime, "main");
        if (result) {
            break;
        }

        if (i_step) {
            m3_RequestSuspend(leg.runtime);
        }

        if (saved) {
            result = m3_LoadSnapshotFromBuffer(leg.runtime, module, saved, savedSize);
            if (result) {
                break;
            }
            result = m3_ResumeRuntime(leg.runtime);
        } else {
            result = m3_CallV(function);
        }

        EndRoundTripLeg(&retired);

        if (result != m3Err_continuationSuspended) {
            break;
        }

        // the first leg starts the call, which is somewhere to have got to
        if (saved and m3_GetResourceUsage(leg.runtime, c_m3Limit_GasUnits) <= 0) {
            result = "a leg stopped where it started";
            break;
        }

        free(saved);
        saved  = NULL;
        result = m3_SaveSnapshotToBuffer(leg.runtime, &saved, &savedSize);
        if (result) {
            break;
        }

        ++*o_numStops;

        if (i_finishUnmetered) {
            i32 finished = 0;

            result = FinishUnmetered(i_wasm, i_size, saved, savedSize, &finished);
            if (not result and finished != i_expected) {
                result = "a pause taken to the end without metering came out wrong";
            }
            if (result) {
                break;
            }
        }

        retired     = leg;
        leg.env     = NULL;
        leg.runtime = NULL;

        if (*o_numStops > 100000) {
            result = "the program stopped making progress";
            break;
        }
    }

    if (not result) {
        *o_result = 0;
        result    = m3_GetResultsV(function, o_result);
    }

    free(saved);

    EndRoundTripLeg(&retired);
    EndRoundTripLeg(&leg);

    return result;
}

#endif

// How the inner call ended, for the case to check after the outer one returns
static M3Result g_reenterResult;

m3ApiRawFunction(CallBackIntoWasm)
{
    IM3Function oob = NULL;

    if (m3_FindFunction(&oob, runtime, "oob") == m3Err_none) {
        *(M3Result*)(_ctx->userdata) = m3_CallV(oob);
    }

    m3ApiSuccess();
}


bool RunTest (int i_argc, const char* i_argv[], cstr_t i_name)
{
    cstr_t option = (i_argc == 2) ? i_argv[1] : NULL;

    bool runningTest = option ? strcmp(option, i_name) == 0 : true;

    if (runningTest) {
        printf("\n    test: %s\n", i_name);
    }

    return runningTest;
}


#if d_m3TestHasThreads

// (module
//   (func $down (call $down))
//   (func (export "run") (call $down)))
//
// A plain call, not a return_call, so every level of it costs a native frame that
// stands until the one below returns - which is never. Nothing else about the module
// grows: no parameters, no locals, no results, so the Wasm stack is barely touched
// and the native stack is what runs out.
static const u8 c_recurseWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
    0x03, 0x03, 0x02, 0x00, 0x00, 0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x01,
    0x0a, 0x0b, 0x02, 0x04, 0x00, 0x10, 0x00, 0x0b, 0x04, 0x00, 0x10, 0x00, 0x0b
};

// How the recursion above ended, on a thread with a stack far smaller than
// d_m3MaxNativeStack. 'measured' says whether the thread's stack could be measured
// at all: where it cannot, the run is skipped rather than taken as a pass, because
// what it would be testing is the process dying. 'stackBytes' is what was measured,
// which the case checks is really smaller than the budget - a thread that quietly
// got a full-sized stack would pass this test while testing nothing.
typedef struct M3TestRecursion {
    bool     measured;
    size_t   stackBytes;
    M3Result result;
} M3TestRecursion;

// Runs on the small thread. Everything it uses is its own: an environment shared
// with the main thread would be fine here, since that thread is blocked in the join,
// but nothing about this test needs it to be.
static
void RunRecursion (M3TestRecursion* io_result)
{
    // m3_NativeStackPtr rather than the address of a local: ASan moves locals to a
    // heap "fake stack", and the difference between one of those and the real stack
    // base is not a stack size at all. This is the same primitive the engine
    // measures with, for the same reason - see m3_config_platforms.h.
    u8* sp   = (u8*)m3_NativeStackPtr();
    u8* base = (u8*)m3_HostStackBase();

    io_result->measured   = (base != NULL);
    io_result->stackBytes = base ? (size_t)(sp - base) : 0;
    io_result->result     = m3Err_none;

    if (not io_result->measured) {
        return;
    }

    IM3Environment env = m3_NewEnvironment();
    if (env == NULL) {
        return;
    }

    // large enough that the native stack is what gives out first
    IM3Runtime runtime = m3_NewRuntime(env, 512 * 1024, NULL);

    if (runtime) {
        IM3Module   module   = NULL;
        IM3Function function = NULL;

        if (m3_ParseModule(env, &module, c_recurseWasm, sizeof(c_recurseWasm)) == m3Err_none) {
            if (m3_LoadModule(runtime, module) == m3Err_none and
                m3_FindFunction(&function, runtime, "run") == m3Err_none) {
                io_result->result = m3_CallV(function);
            } else {
                m3_FreeModule(module);
            }
        }

        m3_FreeRuntime(runtime);
    }

    m3_FreeEnvironment(env);
}

#  define d_m3TestThreadStack  (256 * 1024)

#  if defined(_WIN32)

static
DWORD WINAPI RecursionThread (LPVOID i_param)
{
    RunRecursion((M3TestRecursion*)i_param);
    return 0;
}

// Without STACK_SIZE_PARAM_IS_A_RESERVATION the size below is what Windows commits
// up front and not what it reserves, and the thread would quietly get the whole
// stack named in the executable header instead - which is the stack this test is
// trying not to have.
static
bool RunRecursionOnSmallStack (M3TestRecursion* o_result)
{
    HANDLE thread = CreateThread(NULL, d_m3TestThreadStack, RecursionThread, o_result,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (thread == NULL) {
        return false;
    }

    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return true;
}

#  else

static
void* RecursionThread (void* i_param)
{
    RunRecursion((M3TestRecursion*)i_param);
    return NULL;
}

static
bool RunRecursionOnSmallStack (M3TestRecursion* o_result)
{
    pthread_attr_t attr;

    if (pthread_attr_init(&attr) != 0) {
        return false;
    }

    bool      started = false;
    pthread_t thread;

    if (pthread_attr_setstacksize(&attr, d_m3TestThreadStack) == 0) {
        started = (pthread_create(&thread, &attr, RecursionThread, o_result) == 0);
    }

    pthread_attr_destroy(&attr);

    if (started) {
        pthread_join(thread, NULL);
    }

    return started;
}

#  endif

#endif // d_m3TestHasThreads


#if d_m3HasSnapshots

// Byte i = i * 31 + 7: the input the XXH64 vectors below were taken over
static
void FillTestBytes (u8* o_bytes, size_t i_size)
{
    for (size_t i = 0; i < i_size; ++i) {
        o_bytes[i] = (u8)(i * 31 + 7);
    }
}

// m3_test --pause-points <module.wasm>: every pause point the module's code has,
// one a line, sorted, as the snapshot names them - "<function> <kind> +0x<offset>",
// and for a back edge " -> loop@+0x<offset>". test/pause-points.py works the same
// list out of the instruction stream, and the snapshot format test compares them.
static
int PrintPausePoints (const char* i_path)
{
    static const char* c_kinds[] = { "back-edge", "suspend", "call", "resume", "entry" };

    FILE*          f       = fopen(i_path, "rb");
    u8*            wasm    = NULL;
    long           size    = 0;
    IM3Environment env     = m3_NewEnvironment();
    IM3Runtime     runtime = env ? m3_NewRuntime(env, 64 * 1024, NULL) : NULL;
    IM3Module      module  = NULL;
    M3Result       result  = m3Err_none;

    if (not f or not runtime) {
        fprintf(stderr, "cannot open %s\n", i_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    wasm = (u8*)malloc((size_t)size);
    if (not wasm or fread(wasm, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "cannot read %s\n", i_path);
        return 1;
    }
    fclose(f);

    m3_SetSuspendable(runtime, true);

    result = m3_ParseModule(env, &module, wasm, (u32)size);
    if (not result) {
        result = m3_LoadModule(runtime, module);
    }
#  if d_m3TestLinksWASI
    if (not result) {
        m3_LinkWASI(module);
    }
#  endif
    if (not result) {
        result = m3_CompileModule(module);
    }
    if (result) {
        fprintf(stderr, "%s: %s\n", i_path, result);
        return 1;
    }

    for (u32 i = 0; i < module->numFunctions; ++i) {
        const M3SnapshotMap* map = module->functions[i].snapshotMap;

        for (u32 k = 0; map and k < map->numSafePoints; ++k) {
            const M3SafePoint* point = &map->safePoints[k];

            // a return_call compiled as a call is where the callee takes over:
            // in Wasm, nothing waits there
            if (point->flags & d_m3SafePointTailCall) {
                continue;
            }
            printf("%u %s +0x%x", i, c_kinds[point->kind], point->wasmOffset);
            if (point->kind == safepoint_op) {
                printf(" -> loop@+0x%x", map->blocks[point->aux].wasmOffset);
            }
            printf("\n");
        }
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    free(wasm);

    return 0;
}

#endif // d_m3HasSnapshots


int main (int argc, const char* argv[])
{
#if d_m3HasSnapshots
    if (argc == 3 and not strcmp(argv[1], "--pause-points")) {
        return PrintPausePoints(argv[2]);
    }

    Test(xxh64.known_vectors)
    {
        // XXH64 is a published hash, so these are the reference implementation's
        // answers rather than this port's. Any of them can be recomputed with:
        //
        //   import xxhash
        //   data = bytes((i * 31 + 7) & 0xFF for i in range(4200))
        //   xxhash.xxh64(data[:LENGTH], seed=SEED).hexdigest()
        //
        // The lengths walk every path the algorithm has: under one 32-byte
        // stripe, exactly one, many - and every shape of tail, down to the
        // 8-byte, 4-byte and single-byte steps that finish it.
        // clang-format off
        static const struct {
            u32 length;
            u64 seed;
            u64 hash;
        }
        c_vectors[] = {
            {    0, 0x0000000000000000ULL, 0xEF46DB3751D8E999ULL },
            {    1, 0x0000000000000000ULL, 0xA96C7F0CE858BBB7ULL },
            {    2, 0x0000000000000000ULL, 0xAC378C5993CD5F9AULL },
            {    3, 0x0000000000000000ULL, 0x56E6957632A487F9ULL },
            {    4, 0x0000000000000000ULL, 0xC60D15B1E3FF8F04ULL },
            {    5, 0x0000000000000000ULL, 0x808815858624DD4EULL },
            {    7, 0x0000000000000000ULL, 0xAFBEFC3D6C6F9A8EULL },
            {    8, 0x0000000000000000ULL, 0x3DA5C7AA269683E0ULL },
            {    9, 0x0000000000000000ULL, 0x4B17A9BA9E215C09ULL },
            {   12, 0x0000000000000000ULL, 0x8FE8AB1C1FD0666EULL },
            {   16, 0x0000000000000000ULL, 0xA19AD429B02BC413ULL },
            {   17, 0x0000000000000000ULL, 0xFE9F0FEB7EEEDC09ULL },
            {   31, 0x0000000000000000ULL, 0x4A74F3A1A39AD4A1ULL },
            {   32, 0x0000000000000000ULL, 0x8D57D6A4671CC43DULL },
            {   33, 0x0000000000000000ULL, 0x62C9FD21ED857664ULL },
            {   63, 0x0000000000000000ULL, 0x5C320A0D2707057FULL },
            {   64, 0x0000000000000000ULL, 0x7BBABBC45729D17EULL },
            {   96, 0x0000000000000000ULL, 0x1A4B207385051B55ULL },
            {  128, 0x0000000000000000ULL, 0x725A5B9B3BEDFE94ULL },
            {  129, 0x0000000000000000ULL, 0x28FC8362643627D7ULL },
            {  240, 0x0000000000000000ULL, 0xD430520AE3ED2FC6ULL },
            {  241, 0x0000000000000000ULL, 0xD3F50496D5BF27E0ULL },
            {  256, 0x0000000000000000ULL, 0x7C1FF7B1D57C10D5ULL },
            {  511, 0x0000000000000000ULL, 0xA316A70D395E7BB2ULL },
            { 1024, 0x0000000000000000ULL, 0x149AA44972CDAE00ULL },
            { 1025, 0x0000000000000000ULL, 0x2C9D0B038B4A4B35ULL },
            { 2048, 0x0000000000000000ULL, 0x6A531EF2D65594ECULL },
            { 4199, 0x0000000000000000ULL, 0xB04BAF75BC8CECB1ULL },
            {    0, 0x0000000000000001ULL, 0xD5AFBA1336A3BE4BULL },
            {    1, 0xDEADBEEFCAFEBABEULL, 0x5A2B887A71300464ULL },
            {   16, 0xDEADBEEFCAFEBABEULL, 0x8ACA125C46158C67ULL },
            {   31, 0x0000000000000001ULL, 0xD7AC4F4BEA4E460AULL },
            {   32, 0xDEADBEEFCAFEBABEULL, 0x0BD719FF2B7A1B17ULL },
            {   64, 0x0000000000000001ULL, 0xEE10EEE981202CE9ULL },
            {  241, 0x0000000000000001ULL, 0x6B35C23AE3627A14ULL },
            { 1024, 0xDEADBEEFCAFEBABEULL, 0x89E4805AA689B5A1ULL },
            { 4199, 0x0000000000000001ULL, 0x39C5A4F24938D127ULL },
        };
        // clang-format on

        u8 bytes[4200];
        FillTestBytes(bytes, sizeof(bytes));

        for (size_t v = 0; v < sizeof(c_vectors) / sizeof(c_vectors[0]); ++v) {
            Xxh64 state;
            Xxh64_Init(&state, c_vectors[v].seed);
            Xxh64_Update(&state, bytes, c_vectors[v].length);

            u64 hash = Xxh64_Digest(&state);

            if (hash != c_vectors[v].hash) {
                printf("  vector %u: length %u\n", (unsigned)v, c_vectors[v].length);
            }
            expect(hash == c_vectors[v].hash)
        }
    }


    Test(xxh64.split_input_hashes_the_same)
    {
        // The digest is over the bytes, not over the calls that delivered them.
        // The lengths and chunk sizes here straddle the 32-byte stripe in both
        // directions.
        static const u32 c_lengths[] = { 0, 1, 17, 31, 32, 33, 63, 64, 65, 255, 256, 257, 1024, 2049, 4199 };
        static const u32 c_chunks[]  = { 1, 2, 7, 8, 31, 32, 33, 100, 1000 };

        u8 bytes[4200];
        FillTestBytes(bytes, sizeof(bytes));

        for (size_t l = 0; l < sizeof(c_lengths) / sizeof(c_lengths[0]); ++l) {
            u32   length = c_lengths[l];
            Xxh64 whole;

            Xxh64_Init(&whole, 0);
            Xxh64_Update(&whole, bytes, length);
            u64 expected = Xxh64_Digest(&whole);

            for (size_t c = 0; c < sizeof(c_chunks) / sizeof(c_chunks[0]); ++c) {
                Xxh64 state;
                Xxh64_Init(&state, 0);

                for (u32 offset = 0; offset < length;) {
                    u32 size = (c_chunks[c] < length - offset) ? c_chunks[c] : length - offset;
                    Xxh64_Update(&state, bytes + offset, size);
                    offset += size;
                }

                u64 hash = Xxh64_Digest(&state);

                if (hash != expected) {
                    printf("  length %u in chunks of %u\n", length, c_chunks[c]);
                }
                expect(hash == expected)
            }
        }
    }


    Test(xxh64.digest_leaves_the_state_usable)
    {
        // Reading the digest out must not consume the state: the same state
        // goes on to hash the rest of the input. Both answers below are
        // reference digests, over the first 1024 bytes and then over all 4199.
        u8 bytes[4199];
        FillTestBytes(bytes, sizeof(bytes));

        Xxh64 state;
        Xxh64_Init(&state, 0);

        Xxh64_Update(&state, bytes, 1024);
        u64 interim = Xxh64_Digest(&state);             expect(interim == 0x149AA44972CDAE00ULL)

        Xxh64_Update(&state, bytes + 1024, sizeof(bytes) - 1024);
        u64 hash = Xxh64_Digest(&state);                expect(hash == 0xB04BAF75BC8CECB1ULL)
    }


#endif // d_m3HasSnapshots

    Test(signatures)
    {
        M3Result result;

        IM3FuncType ftype = NULL;

        result = SignatureToFuncType(&ftype, "");                       expect(result == m3Err_malformedFunctionSignature)
        m3_Free(ftype);

        // implicit void return
        result = SignatureToFuncType(&ftype, "()");                     expect(result == m3Err_none)
        m3_Free(ftype);

        result = SignatureToFuncType(&ftype, " v () ");                 expect(result == m3Err_none)
                                                                        expect(ftype->numRets == 0)
                                                                        expect(ftype->numArgs == 0)
        m3_Free(ftype);

        result = SignatureToFuncType(&ftype, "f(IiF)");                 expect(result == m3Err_none)
                                                                        expect(ftype->numRets == 1)
                                                                        expect(ftype->types [0] == c_m3Type_f32)
                                                                        expect(ftype->numArgs == 3)
                                                                        expect(ftype->types [1] == c_m3Type_i64)
                                                                        expect(ftype->types [2] == c_m3Type_i32)
                                                                        expect(ftype->types [3] == c_m3Type_f64)

        IM3FuncType ftype2 = NULL;

        result = SignatureToFuncType(&ftype2, "f(I i F)");              expect(result == m3Err_none);
                                                                        expect(AreFuncTypesEqual (ftype, ftype2));
        m3_Free(ftype);
        m3_Free(ftype2);
    }


    Test(codepages.simple)
    {
        M3Environment env     = { 0 };
        M3Runtime     runtime = { 0 };
        runtime.environment   = &env;

        IM3CodePage page = AcquireCodePage(&runtime);                   expect(page);
                                                                        expect(runtime.numCodePages == 1);
                                                                        expect(runtime.numActiveCodePages == 1);

        IM3CodePage page2 = AcquireCodePage(&runtime);                  expect(page2);
                                                                        expect(runtime.numCodePages == 2);
                                                                        expect(runtime.numActiveCodePages == 2);

        ReleaseCodePage(&runtime, page);                                expect(runtime.numCodePages == 2);
                                                                        expect(runtime.numActiveCodePages == 1);

        ReleaseCodePage(&runtime, page2);                               expect(runtime.numCodePages == 2);
                                                                        expect(runtime.numActiveCodePages == 0);

        Runtime_Release(&runtime);                                      expect(CountCodePages (env.pagesReleased) == 2);
        Environment_Release(&env);                                      expect(CountCodePages (env.pagesReleased) == 0);
    }


    Test(codepages.b)
    {
        const u32   c_numPages  = 2000;
        IM3CodePage pages[2000] = { NULL };

        M3Environment env     = { 0 };
        M3Runtime     runtime = { 0 };
        runtime.environment   = &env;

        u32 numActive = 0;

        for (u32 i = 0; i < 2000000; ++i) {
            u32 index = rand() % c_numPages;   // printf ("%5u ", index);

            if (pages[index] == NULL) {
                //                printf ("acq\n");
                pages[index] = AcquireCodePage(&runtime);
                ++numActive;
            } else {
                //                printf ("rel\n");
                ReleaseCodePage(&runtime, pages[index]);
                pages[index] = NULL;
                --numActive;
            }

            expect(runtime.numActiveCodePages == numActive);
        }

        printf("num pages: %d\n", runtime.numCodePages);

        for (u32 i = 0; i < c_numPages; ++i) {
            if (pages[i]) {
                ReleaseCodePage(&runtime, pages[i]);
                pages[i] = NULL;
                --numActive;                                            expect(runtime.numActiveCodePages == numActive);
            }
        }

        Runtime_Release(&runtime);
        Environment_Release(&env);
    }


    Test(extensions)
    {
        M3Result result;

        IM3Environment env = m3_NewEnvironment();

        IM3Runtime runtime = m3_NewRuntime(env, 1024, NULL);

        IM3Module module = m3_NewModule(env);


        i32 functionIndex = -1;

        u8 wasm[5] = {
            0x04,       // size
            0x00,       // num local defs
            0x41, 0x37, // i32.const= 55
            0x0b        // end block
        };

        // will partially fail (compilation) because module isn't attached to a runtime yet.
        result = m3_InjectFunction(module, &functionIndex, "i()", wasm, true);          expect(result != m3Err_none)
                                                                                        expect(functionIndex >= 0)

        result = m3_LoadModule(runtime, module);                                        expect(result == m3Err_none)

        // try again
        result = m3_InjectFunction(module, &functionIndex, "i()", wasm, true);          expect(result == m3Err_none)

        IM3Function function = m3_GetFunctionByIndex(module, functionIndex);            expect(function)

        if (function) {
            result  = m3_CallV(function);                                               expect(result == m3Err_none)
            u32 ret = 0;
            m3_GetResultsV(function, &ret);                                             expect(ret == 55);
        }

        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
    }

    IM3Environment env = m3_NewEnvironment();


    Test(resources.gas_units)
    {
        IM3Runtime runtime = m3_NewRuntime(env, 65536, NULL);
        expect(runtime);
        expect(m3_SetResourceLimit(runtime, (M3ResourceLimit)99, 1) == m3Err_unknownResourceLimit);
        expect(m3_GetResourceLimit(runtime, (M3ResourceLimit)99) == 0);
        expect(m3_GetResourceUsage(runtime, (M3ResourceLimit)99) == 0);
#if d_m3HasGasMetering
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_GasUnits, UINT64_MAX));
        expect(m3_GetResourceLimit(runtime, c_m3Limit_GasUnits) == INT64_MAX);
        runtime->gasRemaining = -1;
        expect(m3_GetResourceUsage(runtime, c_m3Limit_GasUnits) == (u64)INT64_MAX + 1);
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_GasUnits, 12345));
        expect(m3_GetResourceLimit(runtime, c_m3Limit_GasUnits) == 12345);
        expect(m3_GetResourceUsage(runtime, c_m3Limit_GasUnits) == 0);
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_GasUnits, 0));
        expect(m3_GetResourceLimit(runtime, c_m3Limit_GasUnits) == 0);
#else
        expect(m3_SetResourceLimit(runtime, c_m3Limit_GasUnits, 1) == m3Err_resourceLimitNotSupported);
        expect(m3_SetResourceLimit(runtime, c_m3Limit_GasUnits, 0) == m3Err_resourceLimitNotSupported);
        expect(m3_GetResourceLimit(runtime, c_m3Limit_GasUnits) == 0);
        expect(m3_GetResourceUsage(runtime, c_m3Limit_GasUnits) == 0);
#endif
        m3_FreeRuntime(runtime);
    }

    Test(resources.memory_total)
    {
        IM3Runtime runtime = m3_NewRuntime(env, 65536, NULL);
        IM3Module  first = NULL, second = NULL;
        expect(!m3_ParseModule(env, &first, c_memoryPage, sizeof(c_memoryPage)));
        expect(!m3_LoadModule(runtime, first));
        expect(m3_GetResourceUsage(runtime, c_m3Limit_MemoryBytes) == 65536);
        expect(m3_SetResourceLimit(runtime, c_m3Limit_MemoryBytes, 65535) == m3Err_resourceLimitBelowUsage);
        expect(m3_GetResourceLimit(runtime, c_m3Limit_MemoryBytes) == 0);
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_MemoryBytes, 131072));
        expect(!m3_ParseModule(env, &second, c_memoryPage, sizeof(c_memoryPage)));
        expect(!m3_LoadModule(runtime, second));
        expect(m3_GetResourceUsage(runtime, c_m3Limit_MemoryBytes) == 131072);
        expect(ResizeMemory(runtime, first->memories[0], 2) == m3Err_memoryLimitExceeded);
        expect(first->memories[0]->numPages == 1);
        expect(m3_GetResourceUsage(runtime, c_m3Limit_MemoryBytes) == 131072);
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_MemoryBytes, 0));
        expect(!ResizeMemory(runtime, first->memories[0], 2));
        expect(m3_GetResourceUsage(runtime, c_m3Limit_MemoryBytes) == 196608);
        m3_FreeRuntime(runtime);
    }

    Test(resources.continuation_stacks)
    {
        IM3Runtime runtime = m3_NewRuntime(env, 65536, NULL);
#if d_m3HasStackSwitching
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_Continuations, UINT64_MAX));
        expect(m3_GetResourceLimit(runtime, c_m3Limit_Continuations) == UINT32_MAX);
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_Continuations, 1));
        m3slot_t* previous = NULL;
        for (u32 i = 0; i < 1000; ++i) {
            IM3Continuation cont = Continuation_New(runtime, NULL, NULL);
            expect(cont);
            expect(!Continuation_AcquireStack(runtime, cont));
            expect(m3_GetResourceUsage(runtime, c_m3Limit_Continuations) == 1);
            if (previous) {
                expect(cont->valStack == previous);
            }
            previous              = cont->valStack;
            IM3Continuation moved = Continuation_ForkSuspended(runtime, cont);
            expect(moved and not cont->valStack);
            expect(m3_GetResourceUsage(runtime, c_m3Limit_Continuations) == 1);
            IM3Continuation refused = Continuation_New(runtime, NULL, NULL);
            expect(Continuation_AcquireStack(runtime, refused) == m3Err_continuationLimitExceeded);
            refused->state = cont_consumed;
            moved->state   = cont_consumed;
            Continuation_RecycleStack(runtime, moved);
            expect(m3_GetResourceUsage(runtime, c_m3Limit_Continuations) == 0);
        }
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_Continuations, 2));
        IM3Continuation a = Continuation_New(runtime, NULL, NULL);
        IM3Continuation b = Continuation_New(runtime, NULL, NULL);
        expect(!Continuation_AcquireStack(runtime, a));
        expect(!Continuation_AcquireStack(runtime, b));
        expect(m3_SetResourceLimit(runtime, c_m3Limit_Continuations, 1) == m3Err_resourceLimitBelowUsage);
#else
        expect(m3_SetResourceLimit(runtime, c_m3Limit_Continuations, 1) == m3Err_resourceLimitNotSupported);
        expect(m3_SetResourceLimit(runtime, c_m3Limit_Continuations, 0) == m3Err_resourceLimitNotSupported);
        expect(m3_GetResourceLimit(runtime, c_m3Limit_Continuations) == 0);
        expect(m3_GetResourceUsage(runtime, c_m3Limit_Continuations) == 0);
#endif
        m3_FreeRuntime(runtime);
    }


    Test(resources.shared_objects_and_failed_load)
    {
        // Assembled from regression/limit-resource-{export,import}.wat with bundled WABT.
        const u8 ownerBytes[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x04, 0x04, 0x01, 0x70, 0x00, 0x02, 0x05, 0x03,
            0x01, 0x00, 0x01, 0x07, 0x12, 0x02, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x05,
            0x74, 0x61, 0x62, 0x6c, 0x65, 0x01, 0x00
        };
        const u8 importBytes[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x02, 0x21, 0x02, 0x05, 0x6f, 0x77, 0x6e, 0x65,
            0x72, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x01, 0x05, 0x6f, 0x77, 0x6e, 0x65,
            0x72, 0x05, 0x74,
            0x61, 0x62, 0x6c, 0x65, 0x01, 0x70, 0x00, 0x02
        };
        // regression/limit-memory-multi-init.wat
        const u8 multiBytes[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x05, 0x05, 0x02, 0x00, 0x01, 0x00, 0x02
        };
        IM3Runtime runtime = m3_NewRuntime(env, 65536, NULL);
        IM3Module  owner = NULL, importer = NULL, partial = NULL;
        expect(!m3_ParseModule(env, &owner, ownerBytes, sizeof(ownerBytes)));
        m3_SetModuleName(owner, "owner");
        expect(!m3_LoadModule(runtime, owner));
        expect(m3_GetResourceUsage(runtime, c_m3Limit_TableElements) == 2);
        expect(m3_SetResourceLimit(runtime, c_m3Limit_TableElements, 1) == m3Err_resourceLimitBelowUsage);
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_TableElements, 2));
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_MemoryBytes, 65536));
        expect(!m3_ParseModule(env, &importer, importBytes, sizeof(importBytes)));
        expect(!m3_LoadModule(runtime, importer));
        expect(m3_GetResourceUsage(runtime, c_m3Limit_MemoryBytes) == 65536);
        expect(m3_GetResourceUsage(runtime, c_m3Limit_TableElements) == 2);
        expect(!m3_SetResourceLimit(runtime, c_m3Limit_MemoryBytes, 131072));
        expect(!m3_ParseModule(env, &partial, multiBytes, sizeof(multiBytes)));
        expect(m3_LoadModule(runtime, partial) == m3Err_memoryLimitExceeded);
        expect(m3_GetResourceUsage(runtime, c_m3Limit_MemoryBytes) == 131072);
        expect(m3_GetResourceUsage(runtime, c_m3Limit_TableElements) == 2);
        m3_FreeRuntime(runtime);
    }

    Test(multireturn.a)
    {
        M3Result result;

        IM3Runtime runtime = m3_NewRuntime(env, 1024, NULL);

#if 0
		(module
			(func (result i32 f32)

				i32.const 1234
				f32.const 5678.9
			)

			(export "main" (func 0))
		)
#endif

        u8 wasm[44] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x60, 0x00, 0x02, 0x7f, 0x7d, 0x03, 0x02, 0x01, 0x00, 0x07, 0x08, 0x01, 0x04,
            0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x0a, 0x0c, 0x01, 0x0a, 0x00, 0x41, 0xd2, 0x09, 0x43, 0x33, 0x77, 0xb1, 0x45, 0x0b
        };

        IM3Module module;
        result = m3_ParseModule(env, &module, wasm, 44);                                expect(result == m3Err_none)

        result = m3_LoadModule(runtime, module);                                        expect(result == m3Err_none)

        IM3Function function;
        result = m3_FindFunction(&function, runtime, "main");                           expect(result == m3Err_none)
																						expect(function)
        printf("\n%s\n", result);

        if (function) {
            result = m3_CallV(function);                                                expect(result == m3Err_none)

            i32 ret0 = 0;
            f32 ret1 = 0.;
            m3_GetResultsV(function, &ret0, &ret1);

            printf("%d %f\n", ret0, ret1);
        }

        m3_FreeRuntime(runtime);
    }


    Test(memory.size_not_truncated)
    {
        // A linear memory may be a full 4 GiB, which is one byte too many to
        // count in 32 bits. These used to cast the length down to a u32, so a
        // memory of exactly that size reported zero - m3_GetMemory handed back
        // NULL, and m3ApiCheckMem, which is built on the accessor below, then
        // refused every access into it.
        //
        // No memory is allocated here: the accessor reads the header that sits
        // immediately before the data, so a header on its own is enough to ask.
        M3MemoryHeader header;
        M3_INIT(header);

        void* data = &header + 1;

        header.length = 0;                          expect(m3_GetMemorySizeAt (data) == 0)
        header.length = 65536;                      expect(m3_GetMemorySizeAt (data) == 65536)

        if (sizeof(size_t) > 4) {
            header.length = (size_t)0xFFFFFFFFu;    expect(m3_GetMemorySizeAt (data) == (size_t) 0xFFFFFFFFu)

                        // 4 GiB exactly: what the truncation used to turn into zero
            header.length = (size_t)0x100000000ull;
            expect(m3_GetMemorySizeAt(data) == (size_t)0x100000000ull)
        }

        expect(m3_GetMemorySizeAt(NULL) == 0)
    }


    // A budget counted in bytes is a property of the malloc-backed path. Under
    // d_m3GuardedMemory the backing is committed a system page at a time and the
    // bounds check is the fault itself, so the bytes between the budget and the end
    // of its last page answer instead of trapping - see AllocateMemory.
#if d_m3GuardedMemory
    DisabledTest(memory.limit_caps_bytes_not_pages)
#else
    Test(memory.limit_caps_bytes_not_pages)
#endif
    {
        // memoryLimit is a budget in bytes, not in pages: an MCU rarely has a whole
        // 64 KiB to give, so a memory is backed up to the limit and left short of a
        // page rather than rounded down to none. The page count stays what the
        // module declared - it is what memory.size answers - so the backing and the
        // count deliberately disagree, and every byte past the backing traps.
        //
        // What that costs: the size handed to realloc has to come from the backing,
        // since measuring the old block by the page count would name bytes that were
        // never there.
        const u32 limit = 40000;

        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(runtime)

        if (runtime) {
            runtime->memoryLimit = limit;

            IM3Module module = NULL;
            M3Result  result = m3_ParseModule(env, &module, c_memoryPage, sizeof(c_memoryPage));
            expect(result == m3Err_none)

            result = m3_LoadModule(runtime, module);                     expect(result == m3Err_none)

            size_t backed = 0;
            m3_GetMemory(module, &backed, 0);                            expect(backed == limit)
            expect(m3_GetResourceUsage(runtime, c_m3Limit_MemoryBytes) == limit);
            expect(!m3_SetResourceLimit(runtime, c_m3Limit_MemoryBytes, limit));

            IM3Function size = NULL, grow = NULL, load = NULL;
            m3_FindFunction(&size, runtime, "size");
            m3_FindFunction(&grow, runtime, "grow");
            m3_FindFunction(&load, runtime, "load");
            expect(size and grow and load)

            u32 pages = 0;
            result    = m3_CallV(size);                                  expect(result == m3Err_none)
            m3_GetResultsV(size, &pages);                                expect(pages == 1)

            // the last byte the budget paid for answers, the next one does not
            expect(m3_CallV(load, limit - 1) == m3Err_none)
            expect(m3_CallV(load, limit) == m3Err_trapOutOfBoundsMemoryAccess)

            // growing past the budget stops adding backing, and the page count
            // carries on - which is what makes the two disagree in the first place
            for (u32 i = 0; i < 4; ++i) {
                expect(m3_CallV(grow) == m3Err_none)
            }

            m3_GetMemory(module, &backed, 0);                            expect(backed == limit)

            result = m3_CallV(size);                                     expect(result == m3Err_none)
            m3_GetResultsV(size, &pages);                                expect(pages == 5)

            m3_FreeRuntime(runtime);
        }
    }


    Test(memory.out_of_bounds_inside_a_host_callback)
    {
        // A trap that unwinds out of the middle of a call the host made back into
        // Wasm - which under d_m3GuardedMemory is a fault the system caught, and
        // everywhere else is an ordinary bounds check. Either way the inner call has
        // to report it, the host function has to get control back to say so, and the
        // outer call has to finish normally.
        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);

        expect(runtime)

        if (runtime) {
            IM3Module module = NULL;
            M3Result  result = m3_ParseModule(env, &module, c_reenterWasm, sizeof(c_reenterWasm));
                                                                        expect(result == m3Err_none)
            if (result == m3Err_none) {
                result = m3_LoadModule(runtime, module);                 expect(result == m3Err_none)
            } else {
                m3_FreeModule(module);
            }

            if (result == m3Err_none) {
                result = m3_LinkRawFunctionEx(module, "env", "cb", "v()",
                                              &CallBackIntoWasm, &g_reenterResult);
                                                                        expect(result == m3Err_none)
            }

            IM3Function outer = NULL;

            if (result == m3Err_none) {
                result = m3_FindFunction(&outer, runtime, "outer");      expect(result == m3Err_none)
            }

            if (outer) {
                g_reenterResult = m3Err_none;

                result = m3_CallV(outer);                                expect(result == m3Err_none)
                                                                        expect(g_reenterResult == m3Err_trapOutOfBoundsMemoryAccess)

                // and the runtime is still good for another call afterwards
                result = m3_CallV(outer);                                expect(result == m3Err_none)
            }

            m3_FreeRuntime(runtime);
        }
    }


#if d_m3DeterministicProfile
    Test(deterministic.clock_and_entropy_replay)
    {
        // Under the deterministic profile a fresh runtime is a fresh replay: the
        // clock starts at nothing and the entropy at the same seed, so running the
        // same module again produces the same run.
        IM3Runtime runtime = m3_NewRuntime(env, 1024, NULL);

        expect(runtime)

        if (runtime) {
            // The clock moves one tick per reading, so a guest waiting for time to
            // pass gets there
            u64 first  = Deterministic_Time(runtime);
            u64 second = Deterministic_Time(runtime);                    expect(first == 0)
                                                                        expect(second == first + d_m3DeterministicTickNs)

            // and a wait moves it by exactly what was asked for
            Deterministic_Advance(runtime, 5 * d_m3DeterministicTickNs);

            u64 third = Deterministic_Time(runtime);                     expect(third == second + 6 * d_m3DeterministicTickNs)

            u8 bytes[16];
            Deterministic_Random(runtime, bytes, sizeof(bytes));

            u8 more[16];
            Deterministic_Random(runtime, more, sizeof(more));           // a stream, not one block repeated
                                                                        expect(memcmp(bytes, more, sizeof(bytes)) != 0)

            m3_FreeRuntime(runtime);
        }

        // A second runtime replays the first one exactly
        runtime = m3_NewRuntime(env, 1024, NULL);

        if (runtime) {
            u8 again[16];
            Deterministic_Random(runtime, again, sizeof(again));

            u8         expected[16];
            IM3Runtime other = m3_NewRuntime(env, 1024, NULL);

            if (other) {
                Deterministic_Random(other, expected, sizeof(expected));
                                                                        expect(memcmp(again, expected, sizeof(again)) == 0)
                m3_FreeRuntime(other);
            }
                                                                        expect(Deterministic_Time(runtime) == 0)
            m3_FreeRuntime(runtime);
        }
    }
#endif


#if d_m3MaxNativeStack > 0
    Test(native_stack.limit_is_clamped_to_the_real_stack)
    {
        // The mark d_m3StackLimitEnter sets has to stay inside the stack it is
        // marking. A budget larger than any stack must come back somewhere between
        // the base and the caller's frame; a budget that fits has to be left
        // exactly as it was asked for.
        //
        // The frame address comes from m3_NativeStackPtr, which is what the engine
        // measures with and what reads the real stack under ASan - the address of a
        // local would be on ASan's heap "fake stack" instead. Addresses are compared
        // as integers because the arithmetic deliberately leaves the object it
        // starts in, which a compiler will otherwise warn about.
        void*     frame  = m3_NativeStackPtr();
        uintptr_t sp     = (uintptr_t)frame;
        uintptr_t base   = (uintptr_t)m3_HostStackBase();
        size_t    budget = (size_t)256 * 1024 * 1024;   // no thread's stack is this

        uintptr_t huge  = (uintptr_t)m3_NativeStackLimit(frame, budget);
        uintptr_t tight = (uintptr_t)m3_NativeStackLimit(frame, 4096);

        expect(tight == sp - 4096)

        if (base) {
            expect(huge > base)
            expect(huge < sp)
        } else {
            // nothing to clamp against, so the budget is taken at its word
            expect(huge == sp - budget)
        }
    }
#endif


#if d_m3TestHasThreads && d_m3MaxNativeStack > 0
    Test(native_stack.deep_recursion_on_a_small_thread)
    {
        // The default budget is a good deal larger than the stack this thread is
        // given, so a runaway recursion reaches the end of the real stack long
        // before it reaches the budget. Trapping is the whole assertion: without
        // the stack being measured this does not fail, it kills the process.
        //
        // What the measurement is checked for is that the thread's stack really is
        // smaller than the budget, so that the clamp is what the case exercises - a
        // thread that quietly got a full-sized one would pass while testing nothing.
        // Not that it is d_m3TestThreadStack exactly: a stack size is a request, and
        // musl for one hands back rather more than it was asked for.
        M3TestRecursion recursion = { false, 0, m3Err_none };

        if (RunRecursionOnSmallStack(&recursion)) {
            if (recursion.measured) {
                expect(recursion.stackBytes < (size_t)(d_m3MaxNativeStack))
                expect(recursion.result == m3Err_trapStackOverflow)
            } else {
                printf("skipped: this build cannot measure a thread's stack\n");
            }
        } else {
            printf("skipped: could not start a thread\n");
        }
    }
#endif


    Test(multireturn.branch){
#if 0
			(module
			  (func (param i32) (result i32 i32)

				i32.const 123
				i32.const 456
				i32.const 789

				block (param i32 i32) (result i32 i32 i32)

					local.get 0
					local.get 0

					local.get 0
					br_if 0

					drop

				end

				drop
				drop
			  )

			(export "main" (func 0))
			)
#endif
    }

    // 0x40 encodes the empty block type. It is not a value type, so it has to be
    // refused everywhere a valtype is expected, while still working as a block type.
    Test(parse.empty_block_type_is_not_a_valtype)
    {
        M3Result  result;
        IM3Module module;

        // (type (func (result 0x40)))
        static const u8 c_resultWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x40
        };

        // (type (func (param 0x40)))
        static const u8 c_paramWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x05, 0x01, 0x60, 0x01, 0x40, 0x00
        };

        // (global (mut 0x40) (i32.const 0))
        static const u8 c_globalWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
            0x06, 0x06, 0x01, 0x40, 0x01, 0x41, 0x00, 0x0b
        };

        // (func (result i32) (block) (i32.const 42))  -- 0x40 as a block type
        static const u8 c_blockWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
            0x03, 0x02, 0x01, 0x00,
            0x07, 0x08, 0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00,
            0x0a, 0x09, 0x01, 0x07, 0x00, 0x02, 0x40, 0x0b, 0x41, 0x2a, 0x0b
        };

        result = m3_ParseModule(env, &module, c_resultWasm, sizeof(c_resultWasm));  expect(result == m3Err_invalidTypeId)
        result = m3_ParseModule(env, &module, c_paramWasm, sizeof(c_paramWasm));    expect(result == m3Err_invalidTypeId)
        result = m3_ParseModule(env, &module, c_globalWasm, sizeof(c_globalWasm));  expect(result == m3Err_invalidTypeId)

        // the block form still parses, compiles and runs
        IM3Runtime runtime = m3_NewRuntime(env, 1024, NULL);
        result             = m3_ParseModule(env, &module, c_blockWasm, sizeof(c_blockWasm));
        expect(result == m3Err_none)
        result = m3_LoadModule(runtime, module);                                    expect(result == m3Err_none)

        IM3Function function = NULL;
        result               = m3_FindFunction(&function, runtime, "main");         expect(result == m3Err_none)
        if (function) {
            result  = m3_CallV(function);                                           expect(result == m3Err_none)
            i32 ret = 0;
            m3_GetResultsV(function, &ret);                                         expect(ret == 42)
        }
        m3_FreeRuntime(runtime);
    }

    // Export names belong to the module rather than to what they name, so an
    // entity exported under several names links under every one of them.
    Test(link.one_entity_under_several_names)
    {
        // (module
        //   (import "owner" "m1" (memory 1))
        //   (import "owner" "m2" (memory 1))
        //   (import "owner" "t1" (table 1 funcref))
        //   (import "owner" "g1" (global i32))
        //   (import "owner" "f1" (func (result i32)))
        //   (import "owner" "f4" (func (result i32))))
        static const u8 c_importerWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
            0x02, 0x48, 0x06, 0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x02, 0x6d, 0x31, 0x02, 0x00, 0x01,
            0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x02, 0x6d, 0x32, 0x02, 0x00, 0x01, 0x05, 0x6f, 0x77,
            0x6e, 0x65, 0x72, 0x02, 0x74, 0x31, 0x01, 0x70, 0x00, 0x01, 0x05, 0x6f, 0x77, 0x6e, 0x65,
            0x72, 0x02, 0x67, 0x31, 0x03, 0x7f, 0x00, 0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x02, 0x66,
            0x31, 0x00, 0x00, 0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x02, 0x66, 0x34, 0x00, 0x00
        };
        IM3Runtime  runtime = m3_NewRuntime(env, 1024, NULL);
        IM3Module   owner = NULL, importer = NULL;
        IM3Function function    = NULL;
        uint32_t    memoryIndex = UINT32_MAX;

        expect(!m3_ParseModule(env, &owner, c_linkOwnerWasm, sizeof(c_linkOwnerWasm)));
        m3_SetModuleName(owner, "owner");
        expect(!m3_LoadModule(runtime, owner));
        expect(!m3_ParseModule(env, &importer, c_importerWasm, sizeof(c_importerWasm)));
        expect(!m3_LoadModule(runtime, importer));

        // both memory imports are the one memory
        expect(importer->memories[0] == owner->memories[0]);
        expect(importer->memories[1] == owner->memories[0]);
        expect(importer->tables[0] == owner->tables[0]);
        expect(importer->globals[0].resolved == &owner->globals[0]);
        expect(importer->functions[0].resolved == &owner->functions[0]);
        expect(importer->functions[1].resolved == &owner->functions[0]);

        // and the host's lookups by name see every name as well
        expect(!m3_FindExportedMemory(owner, "m1", &memoryIndex) and memoryIndex == 0);
        expect(m3_FindGlobal(owner, "g1") and m3_FindGlobal(owner, "g1") == m3_FindGlobal(owner, "g2"));
        expect(!m3_FindFunctionIn(&function, owner, "f4") and function == &owner->functions[0]);

        m3_FreeRuntime(runtime);
    }

    // A module that imports something and exports it under a name of its own
    // exports what the import was linked to. One that imports something without
    // exporting it exports nothing, although its slot now holds an entity another
    // module does export.
    Test(link.reexport_under_a_name_of_its_own)
    {
        // (module
        //   (import "owner" "m2" (memory 1))
        //   (import "owner" "t2" (table 1 funcref))
        //   (export "x" (memory 0)))
        static const u8 c_relayWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x02, 0x1a, 0x02, 0x05, 0x6f, 0x77, 0x6e,
            0x65, 0x72, 0x02, 0x6d, 0x32, 0x02, 0x00, 0x01, 0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x02,
            0x74, 0x32, 0x01, 0x70, 0x00, 0x01, 0x07, 0x05, 0x01, 0x01, 0x78, 0x02, 0x00
        };
        // (module (import "relay" "x" (memory 1)))
        static const u8 c_viaXWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x02, 0x0c, 0x01, 0x05, 0x72, 0x65, 0x6c,
            0x61, 0x79, 0x01, 0x78, 0x02, 0x00, 0x01
        };
        // (module (import "relay" "t2" (table 1 funcref)))
        static const u8 c_viaT2Wasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x02, 0x0e, 0x01, 0x05, 0x72, 0x65, 0x6c,
            0x61, 0x79, 0x02, 0x74, 0x32, 0x01, 0x70, 0x00, 0x01
        };
        IM3Runtime runtime = m3_NewRuntime(env, 1024, NULL);
        IM3Module  owner = NULL, relay = NULL, viaX = NULL, viaT2 = NULL;
        uint32_t   memoryIndex = UINT32_MAX;

        expect(!m3_ParseModule(env, &owner, c_linkOwnerWasm, sizeof(c_linkOwnerWasm)));
        m3_SetModuleName(owner, "owner");
        expect(!m3_LoadModule(runtime, owner));
        expect(!m3_ParseModule(env, &relay, c_relayWasm, sizeof(c_relayWasm)));
        m3_SetModuleName(relay, "relay");
        expect(!m3_LoadModule(runtime, relay));

        expect(!m3_FindExportedMemory(relay, "x", &memoryIndex) and memoryIndex == 0);
        expect(m3_FindExportedMemory(relay, "m2", &memoryIndex) == m3Err_unknownMemory);

        expect(!m3_ParseModule(env, &viaX, c_viaXWasm, sizeof(c_viaXWasm)));
        expect(!m3_LoadModule(runtime, viaX));
        expect(viaX->memories[0] == owner->memories[0]);

        expect(!m3_ParseModule(env, &viaT2, c_viaT2Wasm, sizeof(c_viaT2Wasm)));
        expect(m3_LoadModule(runtime, viaT2) == m3Err_unknownImport);

        m3_FreeRuntime(runtime);
    }

#if d_m3HasExceptionHandling || d_m3HasStackSwitching
    Test(link.tag_under_two_names)
    {
        // (module (tag $e) (export "e1" (tag $e)) (export "e2" (tag $e)))
        static const u8 c_ownerWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x0d,
            0x03, 0x01, 0x00, 0x00, 0x07, 0x0b, 0x02, 0x02, 0x65, 0x31, 0x04, 0x00, 0x02, 0x65, 0x32,
            0x04, 0x00
        };
        // (module (import "owner" "e2" (tag)) (import "owner" "e1" (tag)))
        static const u8 c_importerWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x02,
            0x19, 0x02, 0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x02, 0x65, 0x32, 0x04, 0x00, 0x00, 0x05,
            0x6f, 0x77, 0x6e, 0x65, 0x72, 0x02, 0x65, 0x31, 0x04, 0x00, 0x00
        };
        IM3Runtime runtime = m3_NewRuntime(env, 1024, NULL);
        IM3Module  owner = NULL, importer = NULL;

        expect(!m3_ParseModule(env, &owner, c_ownerWasm, sizeof(c_ownerWasm)));
        m3_SetModuleName(owner, "owner");
        expect(!m3_LoadModule(runtime, owner));
        expect(!m3_ParseModule(env, &importer, c_importerWasm, sizeof(c_importerWasm)));
        expect(!m3_LoadModule(runtime, importer));

        expect(importer->tags[0].resolved == &owner->tags[0]);
        expect(importer->tags[1].resolved == &owner->tags[0]);

        m3_FreeRuntime(runtime);
    }
#endif

#if d_m3HasStackSwitching
    // Runs "main" from a module and checks what came back. The stack-switching
    // cases differ only in the module bytes and the expected result.
#  define expectStackSwitchResult(WASM, EXPECTED)                              \
  {                                                                            \
      M3Result   result;                                                       \
      IM3Runtime runtime = m3_NewRuntime(env, 1024, NULL);                     \
      IM3Module  module;                                                       \
      result = m3_ParseModule(env, &module, (WASM), sizeof(WASM));             \
      expect(result == m3Err_none)                                             \
      result = m3_LoadModule(runtime, module);                                 \
      expect(result == m3Err_none)                                             \
      IM3Function function;                                                    \
      result = m3_FindFunction(&function, runtime, "main");                    \
      expect(result == m3Err_none)                                             \
      expect(function)                                                         \
      if (function) {                                                          \
          result = m3_CallV(function);                                         \
          expect(result == m3Err_none)                                         \
          i32 ret = 0;                                                         \
          m3_GetResultsV(function, &ret);                                      \
          expect(ret == (EXPECTED));                                           \
      }                                                                        \
      m3_FreeRuntime(runtime);                                                 \
  }

#  define expectStackSwitchTrap(WASM, EXPECTED)                                \
  {                                                                            \
      M3Result   result;                                                       \
      IM3Runtime runtime = m3_NewRuntime(env, 1024, NULL);                     \
      IM3Module  module;                                                       \
      result = m3_ParseModule(env, &module, (WASM), sizeof(WASM));             \
      expect(result == m3Err_none)                                             \
      result = m3_LoadModule(runtime, module);                                 \
      expect(result == m3Err_none)                                             \
      IM3Function function;                                                    \
      result = m3_FindFunction(&function, runtime, "main");                    \
      expect(result == m3Err_none)                                             \
      expect(function)                                                         \
      if (function) {                                                          \
          result = m3_CallV(function);                                         \
          expect(result == (EXPECTED))                                         \
      }                                                                        \
      m3_FreeRuntime(runtime);                                                 \
  }

    Test(resources.continuation_bind_suspend_and_trap)
    {
        const struct {
            const u8* wasm;
            u32       size;
            M3Result  result;
        } cases[] = {
            { c_ssBasicWasm,         sizeof(c_ssBasicWasm),         m3Err_none                     },
            { c_ssBindWasm,          sizeof(c_ssBindWasm),          m3Err_none                     },
            { c_ssOneShotWasm,       sizeof(c_ssOneShotWasm),       m3Err_trapContinuationConsumed },
            { c_ssFrameOverflowWasm, sizeof(c_ssFrameOverflowWasm), m3Err_trapStackOverflow        },
        };
        for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            IM3Runtime  runtime  = m3_NewRuntime(env, 65536, NULL);
            IM3Module   module   = NULL;
            IM3Function function = NULL;
            expect(!m3_SetResourceLimit(runtime, c_m3Limit_Continuations, 1));
            expect(!m3_ParseModule(env, &module, cases[i].wasm, cases[i].size));
            expect(!m3_LoadModule(runtime, module));
            expect(!m3_FindFunction(&function, runtime, "main"));
            expect(m3_CallV(function) == cases[i].result);
            expect(m3_GetResourceUsage(runtime, c_m3Limit_Continuations) == 0);
            m3_FreeRuntime(runtime);
        }
    }

    Test(stack_switching){
        expectStackSwitchResult(c_ssBasicWasm, 42)
    }

    Test(stack_switching.oneshot){
        expectStackSwitchTrap(c_ssOneShotWasm, m3Err_trapContinuationConsumed)
    }

    Test(stack_switching.bind){
        expectStackSwitchResult(c_ssBindWasm, 42)
    }

    // Every operation that holds a native frame - the calls, op_Loop,
    // op_TryTable - loses it when a suspend unwinds the native stack, and has
    // to have recorded enough on the way past for the resume to stand it up
    // again. These are the shapes where a missing record shows.

    Test(stack_switching.suspend_in_nested_loops){
        // 2 laps of 3 yields: 3+2+1 twice
        expectStackSwitchResult(c_ssNestedLoopsWasm, 12)
    }

    Test(stack_switching.suspend_in_callee_in_loop){
        expectStackSwitchResult(c_ssCallInLoopWasm, 6)
    }

    Test(stack_switching.suspend_in_loop_call_loop){
        expectStackSwitchResult(c_ssLoopCallLoopWasm, 12)
    }

#  if d_m3HasExceptionHandling
    Test(stack_switching.suspend_in_try){
        // the throw lands after the resume, so it can only be caught if the
        // try region was rebuilt along with the rest of the frames
        expectStackSwitchResult(c_ssSuspendInTryWasm, 7)
    }
#  endif

    Test(stack_switching.grow_in_continuation){
        // 3+2+1 yielded, plus the 7 the continuation left in the page it grew
        expectStackSwitchResult(c_ssGrowInContinuationWasm, 13)
    }

    Test(stack_switching.frame_overflow){
        expectStackSwitchTrap(c_ssFrameOverflowWasm, m3Err_trapStackOverflow)
    }

#  if d_m3HasExceptionHandling
    Test(stack_switching.resume_throw){
        // the abort escapes the continuation and is caught by the resumer
        expectStackSwitchResult(c_ssResumeThrowWasm, 107)
    }

    Test(stack_switching.resume_throw_caught_inside){
        // the continuation catches its own abort, in a try region the replay
        // had to rebuild for it
        expectStackSwitchResult(c_ssResumeThrowCaughtWasm, 57)
    }

    Test(stack_switching.resume_throw_unstarted){
        expectStackSwitchResult(c_ssResumeThrowFreshWasm, 9)
    }

    Test(stack_switching.resume_throw_ref){
        expectStackSwitchResult(c_ssResumeThrowRefWasm, 57)
    }
#  endif

    Test(stack_switching.switch_to_peer){
        // the two continuations interleave 1, 2, 3, 4
        expectStackSwitchResult(c_ssSwitchWasm, 1234)
    }

    Test(stack_switching.suspend_past_a_resume){
        expectStackSwitchResult(c_ssNestedPromptWasm, 1745)
    }

    Test(stack_switching.suspend_past_two_resumes){
        expectStackSwitchResult(c_ssNestedPrompt2Wasm, 127465)
    }

    Test(stack_switching.switch_recursive_type){
        // the two tasks interleave 1, 2, 3
        expectStackSwitchResult(c_ssScheduler2Wasm, 123)
    }

    Test(stack_switching.host_suspend_inside_a_resume)
    {
        // $body counts to 1000 in a loop and stores the count in $out; main
        // resumes it under a handler for a tag nothing suspends with. A suspend
        // the host asks for belongs to no handler, so it has to reach the host
        // through that resume rather than stop at it.
        static const u8 c_hostSuspendInResumeWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x06, 0x02, 0x60, 0x00, 0x00, 0x5d, 0x00,
            0x03, 0x03, 0x02, 0x00, 0x00, 0x0d, 0x03, 0x01,
            0x00, 0x00, 0x06, 0x06, 0x01, 0x7f, 0x01, 0x41,
            0x00, 0x0b, 0x07, 0x0e, 0x02, 0x03, 0x6f, 0x75,
            0x74, 0x03, 0x00, 0x04, 0x6d, 0x61, 0x69, 0x6e,
            0x00, 0x01, 0x09, 0x05, 0x01, 0x03, 0x00, 0x01,
            0x00, 0x0a, 0x33, 0x02, 0x1a, 0x01, 0x01, 0x7f,
            0x03, 0x40, 0x20, 0x00, 0x41, 0x01, 0x6a, 0x21,
            0x00, 0x20, 0x00, 0x41, 0xe8, 0x07, 0x49, 0x0d,
            0x00, 0x0b, 0x20, 0x00, 0x24, 0x00, 0x0b, 0x16,
            0x00, 0x02, 0x63, 0x01, 0xd2, 0x00, 0xe0, 0x01,
            0xe3, 0x01, 0x01, 0x00, 0x00, 0x00, 0x0f, 0x0b,
            0x1a, 0x41, 0x7f, 0x24, 0x00, 0x0b
        };

        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(runtime != NULL);
        m3_SetSuspendable(runtime, true);

        IM3Module module = NULL;
        M3Result  r      = m3_ParseModule(env, &module, c_hostSuspendInResumeWasm, sizeof(c_hostSuspendInResumeWasm));
        expect(!r);
        r = m3_LoadModule(runtime, module);
        expect(!r);

        IM3Function function = NULL;
        r                    = m3_FindFunction(&function, runtime, "main");
        expect(!r);

        m3_RequestSuspend(runtime);

        r = m3_CallV(function);
        expect(r == m3Err_continuationSuspended);
        expect(m3_IsSuspended(runtime));

        r = m3_ResumeRuntime(runtime);
        expect(!r);
        expect(!m3_IsSuspended(runtime));

        M3TaggedValue out;
        r = m3_GetGlobal(m3_FindGlobal(module, "out"), &out);
        expect(!r);
        expect(out.value.i32 == 1000);

        m3_FreeRuntime(runtime);
    }

    Test(stack_switching.host_call_while_suspended)
    {
        // run sums 1..1000 in two locals and stores the sum in $out; other
        // overwrites four locals of its own and returns its argument plus one.
        // Called while run is paused, other must not land on run's locals.
        static const u8 c_callWhileSuspendedWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x09, 0x02, 0x60, 0x00, 0x00, 0x60, 0x01,
            0x7f, 0x01, 0x7f, 0x03, 0x03, 0x02, 0x00, 0x01,
            0x06, 0x06, 0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b,
            0x07, 0x15, 0x03, 0x03, 0x6f, 0x75, 0x74, 0x03,
            0x00, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x00, 0x05,
            0x6f, 0x74, 0x68, 0x65, 0x72, 0x00, 0x01, 0x0a,
            0x3d, 0x02, 0x21, 0x01, 0x02, 0x7f, 0x03, 0x40,
            0x20, 0x00, 0x41, 0x01, 0x6a, 0x21, 0x00, 0x20,
            0x01, 0x20, 0x00, 0x6a, 0x21, 0x01, 0x20, 0x00,
            0x41, 0xe8, 0x07, 0x49, 0x0d, 0x00, 0x0b, 0x20,
            0x01, 0x24, 0x00, 0x0b, 0x19, 0x01, 0x04, 0x7f,
            0x41, 0x7f, 0x21, 0x01, 0x41, 0x7f, 0x21, 0x02,
            0x41, 0x7f, 0x21, 0x03, 0x41, 0x7f, 0x21, 0x04,
            0x20, 0x00, 0x41, 0x01, 0x6a, 0x0b
        };

        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(runtime != NULL);
        m3_SetSuspendable(runtime, true);

        IM3Module module = NULL;
        M3Result  r      = m3_ParseModule(env, &module, c_callWhileSuspendedWasm, sizeof(c_callWhileSuspendedWasm));
        expect(!r);
        r = m3_LoadModule(runtime, module);
        expect(!r);

        IM3Function run   = NULL;
        IM3Function other = NULL;
        r                 = m3_FindFunction(&run, runtime, "run");
        expect(!r);
        r = m3_FindFunction(&other, runtime, "other");
        expect(!r);

        m3_RequestSuspend(runtime);

        r = m3_CallV(run);
        expect(r == m3Err_continuationSuspended);

        r = m3_CallV(other, 41);
        expect(!r);
        i32 ret = 0;
        r       = m3_GetResultsV(other, &ret);
        expect(!r);
        expect(ret == 42);
        expect(m3_IsSuspended(runtime));

        r = m3_ResumeRuntime(runtime);
        expect(!r);

        M3TaggedValue out;
        r = m3_GetGlobal(m3_FindGlobal(module, "out"), &out);
        expect(!r);
        expect(out.value.i32 == 500500);

        m3_FreeRuntime(runtime);
    }

#  undef expectStackSwitchResult
#  undef expectStackSwitchTrap
#endif

#if d_m3HasSnapshots

    // A frame far larger than the snapshot writer's old fixed window: 81 i64
    // locals, with the last one holding a value that has to survive the round
    // trip. Saving a fixed number of slots past sp brought this one back as zero.
    // Locals of every float width, live across a loop back edge. They ride the
    // snapshot as bit patterns written little endian, whatever the host is, so
    // a frame that holds one has to come back with the same bits.
    static const u8 c_snapshotFloatsWasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60,
        0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x06, 0x15, 0x02, 0x7c, 0x01, 0x44,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x7d, 0x01, 0x43,
        0x00, 0x00, 0x00, 0x00, 0x0b, 0x07, 0x0f, 0x03, 0x01, 0x64, 0x03, 0x00,
        0x01, 0x66, 0x03, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x00, 0x0a, 0x3c,
        0x01, 0x3a, 0x03, 0x01, 0x7f, 0x01, 0x7c, 0x01, 0x7d, 0x44, 0x18, 0x2d,
        0x44, 0x54, 0xfb, 0x21, 0x09, 0x40, 0x21, 0x01, 0x43, 0x2b, 0x52, 0x9a,
        0x44, 0x21, 0x02, 0x02, 0x40, 0x03, 0x40, 0x20, 0x00, 0x41, 0xc0, 0x9a,
        0x0c, 0x4e, 0x0d, 0x01, 0x20, 0x00, 0x41, 0x01, 0x6a, 0x21, 0x00, 0x0c,
        0x00, 0x0b, 0x0b, 0x20, 0x01, 0x24, 0x00, 0x20, 0x02, 0x24, 0x01, 0x0b
    };

    static const u8 c_snapshotBigFrameWasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x03, 0x02,
        0x01, 0x00, 0x06, 0x06, 0x01, 0x7e, 0x01, 0x42,
        0x00, 0x0b, 0x07, 0x0d, 0x02, 0x03, 0x72, 0x75,
        0x6e, 0x00, 0x00, 0x03, 0x6f, 0x75, 0x74, 0x03,
        0x00, 0x0a, 0x23, 0x01, 0x21, 0x02, 0x01, 0x7f,
        0x51, 0x7e, 0x42, 0xee, 0xff, 0x83, 0x06, 0x21,
        0x51, 0x41, 0x03, 0x21, 0x00, 0x03, 0x40, 0x20,
        0x00, 0x41, 0x01, 0x6b, 0x22, 0x00, 0x0d, 0x00,
        0x0b, 0x20, 0x51, 0x24, 0x00, 0x0b
    };

    static const u8 c_loopCounterWasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x05, 0x03, 0x01, 0x00, 0x01, 0x06, 0x06, 0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x07, 0x14, 0x03, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x01, 0x67, 0x03, 0x00, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x00, 0x0a, 0x19, 0x01, 0x17, 0x00, 0x03, 0x40, 0x23, 0x00, 0x41, 0x01, 0x6a, 0x24, 0x00, 0x23, 0x00, 0x41, 0xa0, 0x8d, 0x06, 0x48, 0x0d, 0x00, 0x0b, 0x23, 0x00, 0x0b
    };

    static const u8 c_nestedCallWasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x03, 0x02, 0x00, 0x00, 0x05, 0x03, 0x01, 0x00, 0x01, 0x06, 0x06, 0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x07, 0x14, 0x03, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x01, 0x67, 0x03, 0x00, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x01, 0x0a, 0x1e, 0x02, 0x17, 0x00, 0x03, 0x40, 0x23, 0x00, 0x41, 0x01, 0x6a, 0x24, 0x00, 0x23, 0x00, 0x41, 0xa0, 0x8d, 0x06, 0x48, 0x0d, 0x00, 0x0b, 0x23, 0x00, 0x0b, 0x04, 0x00, 0x10, 0x00, 0x0b
    };

    Test(snapshot.resource_refusal_can_retry)
    {
        // Assembled from test/snapshot/resource-limits.wat with bundled WABT.
        const u8 wasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x03, 0x03,
            0x02, 0x00, 0x00, 0x04, 0x04, 0x01, 0x70, 0x00, 0x02, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07, 0x11,
            0x02, 0x07, 0x70, 0x72, 0x65, 0x70, 0x61, 0x72, 0x65, 0x00, 0x00, 0x03, 0x72, 0x75, 0x6e, 0x00,
            0x01, 0x0a, 0x19, 0x02, 0x0f, 0x00, 0x41, 0x01, 0x40, 0x00, 0x1a, 0xd0, 0x70, 0x41, 0x02, 0xfc,
            0x0f, 0x00, 0x1a, 0x0b, 0x07, 0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x0b
        };
        IM3Runtime  source = m3_NewRuntime(env, 65536, NULL);
        IM3Runtime  target = m3_NewRuntime(env, 65536, NULL);
        IM3Module   from = NULL, to = NULL;
        IM3Function prepare = NULL, run = NULL;
        void*       bytes = NULL;
        size_t      size  = 0;
        m3_SetSuspendable(source, true);
        expect(!m3_ParseModule(env, &from, wasm, sizeof(wasm)));
        expect(!m3_LoadModule(source, from));
        expect(!m3_FindFunction(&prepare, source, "prepare"));
        expect(!m3_CallV(prepare));
        m3_GetMemory(from, NULL, 0)[0] = 7;
        expect(!m3_FindFunction(&run, source, "run"));
        m3_RequestSuspend(source);
        expect(m3_CallV(run) == m3Err_continuationSuspended);
        expect(!m3_SaveSnapshotToBuffer(source, &bytes, &size));
        expect(!m3_ParseModule(env, &to, wasm, sizeof(wasm)));
        expect(!m3_LoadModule(target, to));
        expect(!m3_SetResourceLimit(target, c_m3Limit_MemoryBytes, 65536));
        expect(!m3_SetResourceLimit(target, c_m3Limit_TableElements, 2));
        expect(m3_LoadSnapshotFromBuffer(target, to, bytes, size) == m3Err_memoryLimitExceeded);
        expect(!to->isUnusable and !to->hasRun);
        expect(m3_GetMemory(to, NULL, 0)[0] == 0);
        expect(m3_GetResourceUsage(target, c_m3Limit_MemoryBytes) == 65536);
        expect(!m3_SetResourceLimit(target, c_m3Limit_MemoryBytes, 131072));
        expect(m3_LoadSnapshotFromBuffer(target, to, bytes, size) == m3Err_tableLimitExceeded);
        expect(!to->isUnusable and !to->hasRun);
        expect(m3_GetResourceUsage(target, c_m3Limit_MemoryBytes) == 65536);
        expect(m3_GetResourceUsage(target, c_m3Limit_TableElements) == 2);
        expect(!m3_SetResourceLimit(target, c_m3Limit_TableElements, 4));
        expect(!m3_LoadSnapshotFromBuffer(target, to, bytes, size));
        expect(m3_GetResourceUsage(target, c_m3Limit_MemoryBytes) == 131072);
        expect(m3_GetResourceUsage(target, c_m3Limit_TableElements) == 4);
        expect(m3_GetMemory(to, NULL, 0)[0] == 7);
        m3_RequestSuspend(target);
        expect(m3_ResumeRuntime(target) == m3Err_continuationSuspended);
        free(bytes);
        m3_FreeRuntime(target);
        m3_FreeRuntime(source);
    }

    Test(snapshot.shared_imports_are_stored_once)
    {
        // An owner exporting a memory as "m" and "n" and a table as "t" and "u",
        // and a module importing each under both names: memory 1 is memory 0, and
        // table 1 is table 0.
        const u8 ownerBytes[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x04, 0x04, 0x01, 0x70, 0x00, 0x02, 0x05, 0x03,
            0x01, 0x00, 0x01, 0x07, 0x11, 0x04, 0x01, 0x6d, 0x02, 0x00, 0x01, 0x6e, 0x02, 0x00, 0x01, 0x74,
            0x01, 0x00, 0x01, 0x75, 0x01, 0x00
        };
        // prepare writes a byte through memory 1 and an element through table 1;
        // run loops until a pause
        const u8 importer[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x02, 0x2f,
            0x04, 0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x01, 0x6d, 0x02, 0x00, 0x01, 0x05, 0x6f, 0x77, 0x6e,
            0x65, 0x72, 0x01, 0x6e, 0x02, 0x00, 0x01, 0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x01, 0x74, 0x01,
            0x70, 0x00, 0x02, 0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x01, 0x75, 0x01, 0x70, 0x00, 0x02, 0x03,
            0x04, 0x03, 0x00, 0x00, 0x00, 0x07, 0x11, 0x02, 0x07, 0x70, 0x72, 0x65, 0x70, 0x61, 0x72, 0x65,
            0x00, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x02, 0x09, 0x05, 0x01, 0x03, 0x00, 0x01, 0x00, 0x0a,
            0x1d, 0x03, 0x02, 0x00, 0x0b, 0x10, 0x00, 0x41, 0x00, 0x41, 0x07, 0x3a, 0x40, 0x01, 0x00, 0x41,
            0x01, 0xd2, 0x00, 0x26, 0x01, 0x0b, 0x07, 0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x0b
        };
        IM3Runtime  source = m3_NewRuntime(env, 65536, NULL);
        IM3Runtime  target = m3_NewRuntime(env, 65536, NULL);
        IM3Runtime  other  = m3_NewRuntime(env, 65536, NULL);
        IM3Module   owner = NULL, from = NULL, to = NULL;
        IM3Function prepare = NULL, run = NULL;
        void*       bytes     = NULL;
        size_t      size      = 0;
        u8*         memoryEnd = NULL;
        M3Result    r;
        m3_SetSuspendable(source, true);
        expect(!m3_ParseModule(env, &owner, ownerBytes, sizeof(ownerBytes)));
        m3_SetModuleName(owner, "owner");
        expect(!m3_LoadModule(source, owner));
        expect(!m3_ParseModule(env, &from, importer, sizeof(importer)));
        expect(!m3_LoadModule(source, from));
        expect(!m3_FindFunction(&prepare, source, "prepare"));
        expect(!m3_CallV(prepare));
        expect(m3_GetMemory(from, NULL, 0)[0] == 7);
        expect(!m3_FindFunction(&run, source, "run"));
        m3_RequestSuspend(source);
        expect(m3_CallV(run) == m3Err_continuationSuspended);
        expect(!m3_SaveSnapshotToBuffer(source, &bytes, &size));

        // past the container header, the Memory section ends with memory 1's
        // record, which is its index and the index it shares: memory 0's
        for (u8* p = (u8*)bytes + 8; p < (u8*)bytes + size;) {
            u8  id     = *p++;
            u32 length = 0, shift = 0;
            do {
                length |= (u32)(*p & 0x7f) << shift;
                shift += 7;
            } while (*p++ & 0x80);
            if (id == 1) {
                memoryEnd = p + length;
            }
            p += length;
        }
        expect(memoryEnd and memoryEnd[-2] == 1 and memoryEnd[-1] == 0);

        // linked the same way, the one memory and table come back once
        expect(!m3_ParseModule(env, &owner, ownerBytes, sizeof(ownerBytes)));
        m3_SetModuleName(owner, "owner");
        expect(!m3_LoadModule(target, owner));
        expect(!m3_ParseModule(env, &to, importer, sizeof(importer)));
        expect(!m3_LoadModule(target, to));
        expect(!m3_LoadSnapshotFromBuffer(target, to, bytes, size));
        expect(m3_GetMemory(to, NULL, 0)[0] == 7);
        expect(m3_GetMemory(to, NULL, 1)[0] == 7);
        expect(to->tables[0]->elements[1] != NULL);
        expect(m3_GetResourceUsage(target, c_m3Limit_MemoryBytes) == 65536);
        expect(m3_GetResourceUsage(target, c_m3Limit_TableElements) == 2);

        // a snapshot claiming memory 1 stands alone describes another program,
        // and is refused before anything is restored
        memoryEnd[-1] = 1;
        expect(!m3_ParseModule(env, &owner, ownerBytes, sizeof(ownerBytes)));
        m3_SetModuleName(owner, "owner");
        expect(!m3_LoadModule(other, owner));
        expect(!m3_ParseModule(env, &to, importer, sizeof(importer)));
        expect(!m3_LoadModule(other, to));
        r = m3_LoadSnapshotFromBuffer(other, to, bytes, size);
        expect(r and !strcmp(r, "the snapshot shares memories between imports differently"));
        expect(!to->isUnusable and !to->hasRun);

        free(bytes);
        m3_FreeRuntime(other);
        m3_FreeRuntime(target);
        m3_FreeRuntime(source);
    }

    Test(snapshot.sharing_follows_linking)
    {
        // Two imports from two modules, which are one memory or two depending on
        // what "relay" is: one re-exporting the owner's memory, or one with a
        // memory of its own. The module is the same either way.
        //
        // (module
        //   (import "owner" "m1" (memory 1))
        //   (import "relay" "m" (memory 1))
        //   (func (export "run") (loop (br 0))))
        const u8 pausing[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x02, 0x18,
            0x02, 0x05, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x02, 0x6d, 0x31, 0x02, 0x00, 0x01, 0x05, 0x72, 0x65,
            0x6c, 0x61, 0x79, 0x01, 0x6d, 0x02, 0x00, 0x01, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07, 0x01, 0x03,
            0x72, 0x75, 0x6e, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x0b
        };
        // (module (import "owner" "m1" (memory 1)) (export "m" (memory 0)))
        const u8 relayShared[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x02, 0x0d, 0x01, 0x05, 0x6f, 0x77, 0x6e, 0x65,
            0x72, 0x02, 0x6d, 0x31, 0x02, 0x00, 0x01, 0x07, 0x05, 0x01, 0x01, 0x6d, 0x02, 0x00
        };
        // (module (memory 1) (export "m" (memory 0)))
        const u8 relayOwn[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07, 0x05, 0x01,
            0x01, 0x6d, 0x02, 0x00
        };
        IM3Runtime  source = m3_NewRuntime(env, 65536, NULL);
        IM3Runtime  target = m3_NewRuntime(env, 65536, NULL);
        IM3Module   owner = NULL, relay = NULL, from = NULL, to = NULL;
        IM3Function run   = NULL;
        void*       bytes = NULL;
        size_t      size  = 0;
        M3Result    r;
        m3_SetSuspendable(source, true);
        expect(!m3_ParseModule(env, &owner, c_linkOwnerWasm, sizeof(c_linkOwnerWasm)));
        m3_SetModuleName(owner, "owner");
        expect(!m3_LoadModule(source, owner));
        expect(!m3_ParseModule(env, &relay, relayOwn, sizeof(relayOwn)));
        m3_SetModuleName(relay, "relay");
        expect(!m3_LoadModule(source, relay));
        expect(!m3_ParseModule(env, &from, pausing, sizeof(pausing)));
        expect(!m3_LoadModule(source, from));
        expect(from->memories[0] != from->memories[1]);
        expect(!m3_FindFunction(&run, source, "run"));
        m3_RequestSuspend(source);
        expect(m3_CallV(run) == m3Err_continuationSuspended);
        expect(!m3_SaveSnapshotToBuffer(source, &bytes, &size));

        // linked together, the file describes another program, and is refused
        // before anything is restored
        expect(!m3_ParseModule(env, &owner, c_linkOwnerWasm, sizeof(c_linkOwnerWasm)));
        m3_SetModuleName(owner, "owner");
        expect(!m3_LoadModule(target, owner));
        expect(!m3_ParseModule(env, &relay, relayShared, sizeof(relayShared)));
        m3_SetModuleName(relay, "relay");
        expect(!m3_LoadModule(target, relay));
        expect(!m3_ParseModule(env, &to, pausing, sizeof(pausing)));
        expect(!m3_LoadModule(target, to));
        expect(to->memories[0] == to->memories[1]);
        r = m3_LoadSnapshotFromBuffer(target, to, bytes, size);
        expect(r and !strcmp(r, "the snapshot shares memories between imports differently"));
        expect(!to->isUnusable and !to->hasRun);

        free(bytes);
        m3_FreeRuntime(target);
        m3_FreeRuntime(source);
    }

    Test(snapshot.continuation_refusal_can_retry)
    {
        IM3Runtime  source = m3_NewRuntime(env, 65536, NULL);
        IM3Runtime  target = m3_NewRuntime(env, 65536, NULL);
        IM3Module   from = NULL, to = NULL;
        IM3Function run   = NULL;
        void*       bytes = NULL;
        size_t      size  = 0;
        m3_SetSuspendable(source, true);
        expect(!m3_ParseModule(env, &from, c_ssNestedPromptWasm, sizeof(c_ssNestedPromptWasm)));
        expect(!m3_LoadModule(source, from));
        expect(!m3_FindFunction(&run, source, "main"));
        m3_RequestSuspend(source);
        M3Result result = m3_CallV(run);
        for (u32 step = 0; step < 100 and result == m3Err_continuationSuspended and
                           m3_GetResourceUsage(source, c_m3Limit_Continuations) < 2;
             ++step) {
            m3_RequestSuspend(source);
            result = m3_ResumeRuntime(source);
        }
        expect(result == m3Err_continuationSuspended);
        expect(m3_GetResourceUsage(source, c_m3Limit_Continuations) >= 2);
        expect(!m3_SaveSnapshotToBuffer(source, &bytes, &size));
        expect(!m3_ParseModule(env, &to, c_ssNestedPromptWasm, sizeof(c_ssNestedPromptWasm)));
        expect(!m3_LoadModule(target, to));
        expect(!m3_SetResourceLimit(target, c_m3Limit_Continuations, 1));
        expect(m3_LoadSnapshotFromBuffer(target, to, bytes, size) == m3Err_continuationLimitExceeded);
        expect(!to->isUnusable and !to->hasRun);
        expect(m3_GetResourceUsage(target, c_m3Limit_Continuations) == 0);
        expect(!m3_SetResourceLimit(target, c_m3Limit_Continuations, 0));
        expect(!m3_LoadSnapshotFromBuffer(target, to, bytes, size));
        expect(m3_GetResourceUsage(target, c_m3Limit_Continuations) >= 2);
        expect(!m3_ResumeRuntime(target));
        expect(m3_GetResourceUsage(target, c_m3Limit_Continuations) == 0);
        free(bytes);
        m3_FreeRuntime(target);
        m3_FreeRuntime(source);
    }

    Test(snapshot.postmortem_without_suspension)
    {
        // run sets $g to 9 and traps, in a runtime that was never made
        // suspendable and so has no root continuation to describe the call
        static const u8 c_trapWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x03, 0x02,
            0x01, 0x00, 0x05, 0x03, 0x01, 0x00, 0x01, 0x06,
            0x06, 0x01, 0x7f, 0x01, 0x41, 0x07, 0x0b, 0x07,
            0x0b, 0x02, 0x01, 0x67, 0x03, 0x00, 0x03, 0x72,
            0x75, 0x6e, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07,
            0x00, 0x41, 0x09, 0x24, 0x00, 0x00, 0x0b, 0x0b,
            0x07, 0x01, 0x00, 0x41, 0x10, 0x0b, 0x01, 0x2a
        };

        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(runtime != NULL);

        IM3Module module = NULL;
        M3Result  r      = m3_ParseModule(env, &module, c_trapWasm, sizeof(c_trapWasm));
        expect(!r);
        r = m3_LoadModule(runtime, module);
        expect(!r);

        IM3Function run = NULL;
        r               = m3_FindFunction(&run, runtime, "run");
        expect(!r);

        r = m3_CallV(run);
        expect(r == m3Err_trapUnreachable);

        void*  bytes = NULL;
        size_t size  = 0;
        r            = m3_SaveSnapshotToBuffer(runtime, &bytes, &size);
        expect(!r);
        expect(bytes != NULL && size >= 8);
        if (bytes && size >= 8) {
            const u8* b = (const u8*)bytes;
            expect(memcmp(b, "\0dmp", 4) == 0);
            expect(b[4] == 1 and b[5] == 0 and b[6] == 0 and b[7] == 0); // version, 4 bytes
            expect(b[8] == 0);                                           // section 0 (Meta)
            u32 pos = 9;                                                 // past the section's size
            while (b[pos++] & 0x80) {
            }
            u32 flags = b[pos++];
            expect(flags == 1);
        }

        // the module it came from has run, so it takes no snapshot at all
        r = m3_LoadSnapshotFromBuffer(runtime, module, bytes, size);
        expect(r && !strcmp(r, "a snapshot restores only into a freshly instantiated module, and this one has run"));

        // and a fresh one refuses it for what it is
        IM3Runtime fresh = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(fresh != NULL);

        IM3Module freshModule = NULL;
        r                     = m3_ParseModule(env, &freshModule, c_trapWasm, sizeof(c_trapWasm));
        expect(!r);
        r = m3_LoadModule(fresh, freshModule);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(fresh, freshModule, bytes, size);
        expect(r && !strcmp(r, "postmortem snapshots cannot be resumed"));

        free(bytes);
        m3_FreeRuntime(fresh);
        m3_FreeRuntime(runtime);
    }

    Test(snapshot.restore_does_not_rerun_start)
    {
        // the start function adds one to $n; run loops, and get returns $n
        static const u8 c_startWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x08, 0x02, 0x60, 0x00, 0x00, 0x60, 0x00,
            0x01, 0x7f, 0x03, 0x04, 0x03, 0x00, 0x00, 0x01,
            0x06, 0x06, 0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b,
            0x07, 0x11, 0x03, 0x01, 0x6e, 0x03, 0x00, 0x03,
            0x72, 0x75, 0x6e, 0x00, 0x01, 0x03, 0x67, 0x65,
            0x74, 0x00, 0x02, 0x08, 0x01, 0x00, 0x0a, 0x27,
            0x03, 0x09, 0x00, 0x23, 0x00, 0x41, 0x01, 0x6a,
            0x24, 0x00, 0x0b, 0x16, 0x01, 0x01, 0x7f, 0x03,
            0x40, 0x20, 0x00, 0x41, 0x01, 0x6a, 0x21, 0x00,
            0x20, 0x00, 0x41, 0xe8, 0x07, 0x49, 0x0d, 0x00,
            0x0b, 0x0b, 0x04, 0x00, 0x23, 0x00, 0x0b
        };

        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_startWasm, sizeof(c_startWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function run = NULL;
        r               = m3_FindFunction(&run, rt1, "run");
        expect(!r);

        // the call runs the start function first, so $n is 1 when it pauses
        m3_RequestSuspend(rt1);
        r = m3_CallV(run);
        expect(r == m3Err_continuationSuspended);

        void*  snapBytes = NULL;
        size_t snapSize  = 0;
        r                = m3_SaveSnapshotToBuffer(rt1, &snapBytes, &snapSize);
        expect(!r);
        m3_FreeRuntime(rt1);

        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_startWasm, sizeof(c_startWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, snapBytes, snapSize);
        expect(!r);
        free(snapBytes);

        r = m3_ResumeRuntime(rt2);
        expect(!r);

        IM3Function get = NULL;
        r               = m3_FindFunction(&get, rt2, "get");
        expect(!r);
        r = m3_CallV(get);
        expect(!r);
        i32 n = 0;
        m3_GetResultsV(get, &n);
        expect(n == 1);

        m3_FreeRuntime(rt2);
    }

    Test(snapshot.suspended_start_is_finished_by_resume)
    {
        // the start function loops before adding one to $n; get returns $n
        static const u8 c_startLoopWasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x08, 0x02, 0x60, 0x00, 0x00, 0x60, 0x00,
            0x01, 0x7f, 0x03, 0x03, 0x02, 0x00, 0x01, 0x06,
            0x06, 0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x07,
            0x0b, 0x02, 0x01, 0x6e, 0x03, 0x00, 0x03, 0x67,
            0x65, 0x74, 0x00, 0x01, 0x08, 0x01, 0x00, 0x0a,
            0x24, 0x02, 0x1d, 0x01, 0x01, 0x7f, 0x03, 0x40,
            0x20, 0x00, 0x41, 0x01, 0x6a, 0x21, 0x00, 0x20,
            0x00, 0x41, 0xe8, 0x07, 0x49, 0x0d, 0x00, 0x0b,
            0x23, 0x00, 0x41, 0x01, 0x6a, 0x24, 0x00, 0x0b,
            0x04, 0x00, 0x23, 0x00, 0x0b
        };

        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(runtime != NULL);
        m3_SetSuspendable(runtime, true);

        IM3Module module = NULL;
        M3Result  r      = m3_ParseModule(env, &module, c_startLoopWasm, sizeof(c_startLoopWasm));
        expect(!r);
        r = m3_LoadModule(runtime, module);
        expect(!r);

        m3_RequestSuspend(runtime);
        r = m3_RunStart(module);
        expect(r == m3Err_continuationSuspended);

        r = m3_ResumeRuntime(runtime);
        expect(!r);

        IM3Function get = NULL;
        r               = m3_FindFunction(&get, runtime, "get");
        expect(!r);
        r = m3_CallV(get);
        expect(!r);
        i32 n = 0;
        m3_GetResultsV(get, &n);
        expect(n == 1);

        m3_FreeRuntime(runtime);
    }

    Test(snapshot.frame_larger_than_the_save_window)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_snapshotBigFrameWasm, sizeof(c_snapshotBigFrameWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function func1 = NULL;
        r                 = m3_FindFunction(&func1, rt1, "run");
        expect(!r);

        m3_RequestSuspend(rt1);

        r = m3_Call(func1, 0, NULL);
        expect(r == m3Err_continuationSuspended);
        expect(m3_IsSuspended(rt1));

        void*  snapBytes = NULL;
        size_t snapSize  = 0;
        r                = m3_SaveSnapshotToBuffer(rt1, &snapBytes, &snapSize);
        expect(!r);
        expect(snapSize >= 8);
        expect(memcmp(snapBytes, "\0dmp", 4) == 0);
        expect(((u8*)snapBytes)[4] == 1 and ((u8*)snapBytes)[5] == 0 and
               ((u8*)snapBytes)[6] == 0 and ((u8*)snapBytes)[7] == 0);

        m3_FreeRuntime(rt1);

        // a fresh runtime, so the only way the locals get back is the snapshot
        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_snapshotBigFrameWasm, sizeof(c_snapshotBigFrameWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, snapBytes, snapSize);
        expect(!r);

        r = m3_ResumeRuntime(rt2);
        expect(!r);

        IM3Global g = m3_FindGlobal(mod2, "out");
        expect(g != NULL);

        M3TaggedValue gv;
        r = m3_GetGlobal(g, &gv);
        expect(!r);
        expect(gv.value.i64 == 0xC0FFEE);

        void*  postmortemBytes = NULL;
        size_t postmortemSize  = 0;
        r                      = m3_SaveSnapshotToBuffer(rt2, &postmortemBytes, &postmortemSize);
        expect(!r);
        expect(postmortemBytes != NULL && postmortemSize > 0);

        IM3Runtime rt3 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt3 != NULL);

        IM3Module mod3 = NULL;
        r              = m3_ParseModule(env, &mod3, c_snapshotBigFrameWasm, sizeof(c_snapshotBigFrameWasm));
        expect(!r);
        r = m3_LoadModule(rt3, mod3);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt3, mod3, postmortemBytes, postmortemSize);
        expect(r && !strcmp(r, "postmortem snapshots cannot be resumed"));

        free(snapBytes);
        free(postmortemBytes);
        m3_FreeRuntime(rt3);
        m3_FreeRuntime(rt2);
    }

    Test(snapshot.suspend_and_resume_loop)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function func1 = NULL;
        r                 = m3_FindFunction(&func1, rt1, "run");
        expect(!r);

        // Arm suspension request before running
        m3_RequestSuspend(rt1);

        r = m3_Call(func1, 0, NULL);
        expect(r == m3Err_continuationSuspended);
        expect(m3_IsSuspended(rt1));

        // Save snapshot to buffer
        void*  snapBytes = NULL;
        size_t snapSize  = 0;
        r                = m3_SaveSnapshotToBuffer(rt1, &snapBytes, &snapSize);
        expect(!r);
        expect(snapBytes != NULL && snapSize > 0);

        m3_FreeRuntime(rt1);

        // Restore snapshot into a brand new runtime
        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, snapBytes, snapSize);
        expect(!r);
        expect(m3_IsSuspended(rt2));

        free(snapBytes);

        // Resume execution to completion
        r = m3_ResumeRuntime(rt2);
        expect(!r);
        expect(!m3_IsSuspended(rt2));

        // Verify global value reached 100000
        IM3Global g = m3_FindGlobal(mod2, "g");
        expect(g != NULL);
        M3TaggedValue tv;
        r = m3_GetGlobal(g, &tv);
        expect(!r);
        expect(tv.type == c_m3Type_i32 && tv.value.i32 == 100000);

        m3_FreeRuntime(rt2);
    }

#  if d_m3HasGasMetering
    Test(snapshot.gas_out_suspend_and_resume)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetResourceLimit(rt1, c_m3Limit_GasUnits, (uint64_t)((100) * M3_GAS_UNITS_PER_GAS));
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function func1 = NULL;
        r                 = m3_FindFunction(&func1, rt1, "run");
        expect(!r);

        // Run until out of gas
        r = m3_Call(func1, 0, NULL);
        expect(r == m3Err_continuationSuspended);
        expect(m3_IsSuspended(rt1));

        // Verify partial progress was made
        IM3Global g1 = m3_FindGlobal(mod1, "g");
        expect(g1 != NULL);
        M3TaggedValue tv1;
        r = m3_GetGlobal(g1, &tv1);
        expect(!r);
        expect(tv1.type == c_m3Type_i32 && tv1.value.i32 > 0 && tv1.value.i32 < 100000);

        // Save snapshot to buffer
        void*  snapBytes = NULL;
        size_t snapSize  = 0;
        r                = m3_SaveSnapshotToBuffer(rt1, &snapBytes, &snapSize);
        expect(!r);
        expect(snapBytes != NULL && snapSize > 0);

        m3_FreeRuntime(rt1);

        // Restore snapshot into fresh runtime with replenished gas
        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);
        m3_SetResourceLimit(rt2, c_m3Limit_GasUnits, (uint64_t)(UINT64_C(10000000) * M3_GAS_UNITS_PER_GAS));

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, snapBytes, snapSize);
        expect(!r);
        expect(m3_IsSuspended(rt2));

        free(snapBytes);

        // Resume to completion
        r = m3_ResumeRuntime(rt2);
        expect(!r);
        expect(!m3_IsSuspended(rt2));

        IM3Global g2 = m3_FindGlobal(mod2, "g");
        expect(g2 != NULL);
        M3TaggedValue tv2;
        r = m3_GetGlobal(g2, &tv2);
        expect(!r);
        expect(tv2.type == c_m3Type_i32 && tv2.value.i32 == 100000);

        m3_FreeRuntime(rt2);
    }
#  endif

    Test(snapshot.nested_call_suspend)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_nestedCallWasm, sizeof(c_nestedCallWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function func1 = NULL;
        r                 = m3_FindFunction(&func1, rt1, "run");
        expect(!r);

        m3_RequestSuspend(rt1);

        r = m3_Call(func1, 0, NULL);
        expect(r == m3Err_continuationSuspended);
        expect(m3_IsSuspended(rt1));

        void*  snapBytes = NULL;
        size_t snapSize  = 0;
        r                = m3_SaveSnapshotToBuffer(rt1, &snapBytes, &snapSize);
        expect(!r);
        expect(snapBytes != NULL && snapSize > 0);

        m3_FreeRuntime(rt1);

        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_nestedCallWasm, sizeof(c_nestedCallWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, snapBytes, snapSize);
        expect(!r);
        expect(m3_IsSuspended(rt2));

        free(snapBytes);

        r = m3_ResumeRuntime(rt2);
        expect(!r);

        IM3Global g = m3_FindGlobal(mod2, "g");
        expect(g != NULL);
        M3TaggedValue tv;
        r = m3_GetGlobal(g, &tv);
        expect(!r);
        expect(tv.type == c_m3Type_i32 && tv.value.i32 == 100000);

        m3_FreeRuntime(rt2);
    }

    Test(snapshot.sparse_compression)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        // Populate linear memory with sparse data
        size_t memSize = 0;
        u8*    mem     = m3_GetMemory(mod1, &memSize, 0);
        expect(mem != NULL && memSize >= 65536);

        // Write patterns
        memset(mem + 500, 0xFF, 256);  // run of 0xFF >= 128 -> should be compressed with CHUNK_FILL_FF
        memset(mem + 1000, 0x42, 64);  // raw chunk
        memset(mem + 2000, 0x99, 10);  // small raw chunk

        IM3Function func1 = NULL;
        r                 = m3_FindFunction(&func1, rt1, "run");
        expect(!r);

        m3_RequestSuspend(rt1);
        r = m3_Call(func1, 0, NULL);
        expect(r == m3Err_continuationSuspended);

        void*  snapBytes = NULL;
        size_t snapSize  = 0;
        r                = m3_SaveSnapshotToBuffer(rt1, &snapBytes, &snapSize);
        expect(!r);
        expect(snapBytes != NULL);

        // Sparse compression verification: 64KB memory with only ~74 active bytes
        // should be tightly compressed to less than 1500 bytes!
        expect(snapSize < 1500);

        m3_FreeRuntime(rt1);

        // Restore into fresh runtime
        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, snapBytes, snapSize);
        expect(!r);

        free(snapBytes);

        // Verify restored linear memory
        size_t memSize2 = 0;
        u8*    mem2     = m3_GetMemory(mod2, &memSize2, 0);
        expect(mem2 != NULL && memSize2 == memSize);

        // Verify zeros before 500
        for (u32 i = 0; i < 500; ++i) {
            expect(mem2[i] == 0x00);
        }
        // Verify 0xFF fill
        for (u32 i = 500; i < 756; ++i) {
            expect(mem2[i] == 0xFF);
        }
        // Verify 0x42 data
        for (u32 i = 1000; i < 1064; ++i) {
            expect(mem2[i] == 0x42);
        }
        // Verify 0x99 data
        for (u32 i = 2000; i < 2010; ++i) {
            expect(mem2[i] == 0x99);
        }

        m3_FreeRuntime(rt2);
    }

    // funcrefs in a local, a table and a global, carried across a loop:
    // 0 + 1 + ... + 19, then 7 and 11 called through the table
    static const u8 c_snapshotFuncrefsWasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03,
        0x04, 0x03, 0x00, 0x00, 0x00, 0x04, 0x04, 0x01,
        0x70, 0x00, 0x01, 0x06, 0x06, 0x01, 0x70, 0x01,
        0xd0, 0x70, 0x0b, 0x07, 0x08, 0x01, 0x04, 0x6d,
        0x61, 0x69, 0x6e, 0x00, 0x02, 0x09, 0x06, 0x01,
        0x03, 0x00, 0x02, 0x00, 0x01, 0x0a, 0x50, 0x03,
        0x04, 0x00, 0x41, 0x07, 0x0b, 0x04, 0x00, 0x41,
        0x0b, 0x0b, 0x44, 0x02, 0x01, 0x70, 0x02, 0x7f,
        0xd2, 0x00, 0x21, 0x00, 0xd2, 0x01, 0x24, 0x00,
        0x03, 0x40, 0x20, 0x02, 0x20, 0x01, 0x6a, 0x21,
        0x02, 0x20, 0x01, 0x41, 0x01, 0x6a, 0x21, 0x01,
        0x20, 0x01, 0x41, 0x14, 0x49, 0x0d, 0x00, 0x0b,
        0x41, 0x00, 0x20, 0x00, 0x26, 0x00, 0x20, 0x02,
        0x41, 0x00, 0x11, 0x00, 0x00, 0x6a, 0x21, 0x02,
        0x41, 0x00, 0x23, 0x00, 0x26, 0x00, 0x20, 0x02,
        0x41, 0x00, 0x11, 0x00, 0x00, 0x6a, 0x0b
    };

    // recursive fib, called on 12 by "main": no loop anywhere, so the only
    // pause points it passes are its functions' entries
    static const u8 c_snapshotFibWasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x0a, 0x02, 0x60, 0x01, 0x7f, 0x01, 0x7f,
        0x60, 0x00, 0x01, 0x7f, 0x03, 0x03, 0x02, 0x00,
        0x01, 0x07, 0x08, 0x01, 0x04, 0x6d, 0x61, 0x69,
        0x6e, 0x00, 0x01, 0x0a, 0x26, 0x02, 0x1d, 0x00,
        0x20, 0x00, 0x41, 0x02, 0x49, 0x04, 0x40, 0x20,
        0x00, 0x0f, 0x0b, 0x20, 0x00, 0x41, 0x02, 0x6b,
        0x10, 0x00, 0x20, 0x00, 0x41, 0x01, 0x6b, 0x10,
        0x00, 0x6a, 0x0f, 0x0b, 0x06, 0x00, 0x41, 0x0c,
        0x10, 0x00, 0x0b
    };

    // a funcref carried round a loop as its parameter, which every back edge
    // writes into the loop's own landing pad: 0 + 1 + ... + 29, and 1 more
    // called through it
    static const u8 c_snapshotLoopParamWasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x0a, 0x02, 0x60, 0x00, 0x01, 0x7f, 0x60,
        0x01, 0x70, 0x01, 0x70, 0x03, 0x03, 0x02, 0x00,
        0x00, 0x04, 0x04, 0x01, 0x70, 0x00, 0x01, 0x07,
        0x08, 0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00,
        0x01, 0x09, 0x05, 0x01, 0x03, 0x00, 0x01, 0x00,
        0x0a, 0x37, 0x02, 0x04, 0x00, 0x41, 0x01, 0x0b,
        0x30, 0x02, 0x02, 0x7f, 0x01, 0x70, 0xd2, 0x00,
        0x03, 0x01, 0x20, 0x01, 0x20, 0x00, 0x6a, 0x21,
        0x01, 0x20, 0x00, 0x41, 0x01, 0x6a, 0x21, 0x00,
        0x20, 0x00, 0x41, 0x1e, 0x49, 0x0d, 0x00, 0x0b,
        0x21, 0x02, 0x41, 0x00, 0x20, 0x02, 0x26, 0x00,
        0x20, 0x01, 0x41, 0x00, 0x11, 0x00, 0x00, 0x6a,
        0x0b
    };

#  if d_m3HasExceptionHandling && d_m3HasGasMetering
    // an exnref caught into a local, carried across a loop of 10 and thrown
    // again: its payload of 40, plus the loop count
    static const u8 c_snapshotExnrefWasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x0e, 0x03, 0x60, 0x01, 0x7f, 0x00, 0x60,
        0x00, 0x01, 0x7f, 0x60, 0x00, 0x02, 0x7f, 0x69,
        0x03, 0x02, 0x01, 0x01, 0x0d, 0x03, 0x01, 0x00,
        0x00, 0x07, 0x08, 0x01, 0x04, 0x6d, 0x61, 0x69,
        0x6e, 0x00, 0x00, 0x0a, 0x40, 0x01, 0x3e, 0x02,
        0x01, 0x69, 0x02, 0x7f, 0x02, 0x02, 0x1f, 0x40,
        0x01, 0x01, 0x00, 0x00, 0x41, 0x28, 0x08, 0x00,
        0x0b, 0x00, 0x0b, 0x21, 0x00, 0x1a, 0x03, 0x40,
        0x20, 0x01, 0x41, 0x01, 0x6a, 0x21, 0x01, 0x20,
        0x01, 0x41, 0x0a, 0x49, 0x0d, 0x00, 0x0b, 0x02,
        0x7f, 0x1f, 0x40, 0x01, 0x00, 0x00, 0x00, 0x20,
        0x00, 0x0a, 0x0b, 0x00, 0x0b, 0x21, 0x02, 0x20,
        0x02, 0x20, 0x01, 0x6a, 0x0b
    };
#  endif

#  if d_m3HasGasMetering
    // Every program here, stopped at every pause point it reaches and carried
    // to a new runtime each time. The stack switching ones stop inside the
    // continuations they run, so the stops take whole chains of them along.
    Test(snapshot.round_trip_at_every_pause_point)
    {
        static const struct {
            const char* name;
            const u8*   wasm;
            u32         size;
            i32         expected;
        } c_programs[] = {
            { "funcrefs",             c_snapshotFuncrefsWasm,     sizeof(c_snapshotFuncrefsWasm),     208    },
            { "loop_param",           c_snapshotLoopParamWasm,    sizeof(c_snapshotLoopParamWasm),    436    },
            { "fib",                  c_snapshotFibWasm,          sizeof(c_snapshotFibWasm),          144    },
#    if d_m3HasExceptionHandling
            { "exnref",               c_snapshotExnrefWasm,       sizeof(c_snapshotExnrefWasm),       50     },
            { "suspend_in_try",       c_ssSuspendInTryWasm,       sizeof(c_ssSuspendInTryWasm),       7      },
            { "resume_throw",         c_ssResumeThrowWasm,        sizeof(c_ssResumeThrowWasm),        107    },
            { "resume_throw_caught",  c_ssResumeThrowCaughtWasm,  sizeof(c_ssResumeThrowCaughtWasm),  57     },
            { "resume_throw_fresh",   c_ssResumeThrowFreshWasm,   sizeof(c_ssResumeThrowFreshWasm),   9      },
            { "resume_throw_ref",     c_ssResumeThrowRefWasm,     sizeof(c_ssResumeThrowRefWasm),     57     },
#    endif
            { "basic",                c_ssBasicWasm,              sizeof(c_ssBasicWasm),              42     },
            { "bind",                 c_ssBindWasm,               sizeof(c_ssBindWasm),               42     },
            { "nested_loops",         c_ssNestedLoopsWasm,        sizeof(c_ssNestedLoopsWasm),        12     },
            { "call_in_loop",         c_ssCallInLoopWasm,         sizeof(c_ssCallInLoopWasm),         6      },
            { "loop_call_loop",       c_ssLoopCallLoopWasm,       sizeof(c_ssLoopCallLoopWasm),       12     },
            { "grow_in_continuation", c_ssGrowInContinuationWasm, sizeof(c_ssGrowInContinuationWasm), 13     },
            { "switch",               c_ssSwitchWasm,             sizeof(c_ssSwitchWasm),             1234   },
            { "nested_prompt",        c_ssNestedPromptWasm,       sizeof(c_ssNestedPromptWasm),       1745   },
            { "nested_prompt2",       c_ssNestedPrompt2Wasm,      sizeof(c_ssNestedPrompt2Wasm),      127465 },
            { "scheduler2",           c_ssScheduler2Wasm,         sizeof(c_ssScheduler2Wasm),         123    },
        };

        u32 totalStops = 0;

        for (u32 i = 0; i < sizeof(c_programs) / sizeof(c_programs[0]); ++i) {
            i32      value    = 0;
            u32      numStops = 0;
            M3Result r        = RunInRoundTrips(c_programs[i].wasm, c_programs[i].size, &value, &numStops, true, false, 0);

            if (r or value != c_programs[i].expected) {
                printf("  %s: %s, result %d, %u stops\n", c_programs[i].name, r ? r : "ok", value, numStops);
            }

            expect(!r);
            expect(value == c_programs[i].expected);

            totalStops += numStops;
        }

        // every call and every lap of a loop is one
        expect(totalStops > 100);
    }

    // Recursion with no loop anywhere passes a pause point on every call - the
    // entry of the function it calls - so a gas budget pauses it as it would a
    // loop, and each pause goes on in a runtime that is not metering
    Test(snapshot.gas_pauses_recursion)
    {
        i32      value    = 0;
        u32      numStops = 0;
        M3Result r        = RunInRoundTrips(c_snapshotFibWasm, sizeof(c_snapshotFibWasm), &value, &numStops, false, true, 144);

        expect(!r);
        expect(value == 144);
        expect(numStops > 10);
    }

    // Running out of gas pauses at the next pause point, which is one a runtime
    // that is not metering has too: a snapshot taken at any such pause, in any
    // of these programs, has to go on there and come out the same
    Test(snapshot.gas_pause_resumes_unmetered)
    {
        static const struct {
            const char* name;
            const u8*   wasm;
            u32         size;
            i32         expected;
        } c_programs[] = {
            { "funcrefs",      c_snapshotFuncrefsWasm,  sizeof(c_snapshotFuncrefsWasm),  208  },
            { "loop_param",    c_snapshotLoopParamWasm, sizeof(c_snapshotLoopParamWasm), 436  },
#    if d_m3HasExceptionHandling
            { "exnref",        c_snapshotExnrefWasm,    sizeof(c_snapshotExnrefWasm),    50   },
            { "resume_throw",  c_ssResumeThrowWasm,     sizeof(c_ssResumeThrowWasm),     107  },
#    endif
            { "bind",          c_ssBindWasm,            sizeof(c_ssBindWasm),            42   },
            { "call_in_loop",  c_ssCallInLoopWasm,      sizeof(c_ssCallInLoopWasm),      6    },
            { "switch",        c_ssSwitchWasm,          sizeof(c_ssSwitchWasm),          1234 },
            { "nested_prompt", c_ssNestedPromptWasm,    sizeof(c_ssNestedPromptWasm),    1745 },
            { "scheduler2",    c_ssScheduler2Wasm,      sizeof(c_ssScheduler2Wasm),      123  },
        };

        for (u32 i = 0; i < sizeof(c_programs) / sizeof(c_programs[0]); ++i) {
            i32      value    = 0;
            u32      numStops = 0;
            M3Result r        = RunInRoundTrips(c_programs[i].wasm, c_programs[i].size, &value, &numStops, false, true,
                                                c_programs[i].expected);

            if (r or value != c_programs[i].expected) {
                printf("  %s: %s, result %d, %u stops\n", c_programs[i].name, r ? r : "ok", value, numStops);
            }

            expect(!r);
            expect(value == c_programs[i].expected);
            expect(numStops > 0);
        }
    }
#  endif

    // Stopped on the back edge itself, where the loop's parameter has just been
    // written into its landing pad and nothing else holds it
    Test(snapshot.loop_param_across_back_edge)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_snapshotLoopParamWasm, sizeof(c_snapshotLoopParamWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function main1 = NULL;
        r                 = m3_FindFunction(&main1, rt1, "main");
        expect(!r);

        m3_RequestSuspend(rt1);
        r = m3_CallV(main1);
        expect(r == m3Err_continuationSuspended);

        void*  bytes = NULL;
        size_t size  = 0;
        r            = m3_SaveSnapshotToBuffer(rt1, &bytes, &size);
        expect(!r);

        // kept until the end, so the restore cannot land on its addresses
        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_snapshotLoopParamWasm, sizeof(c_snapshotLoopParamWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, bytes, size);
        expect(!r);
        free(bytes);

        r = m3_ResumeRuntime(rt2);
        expect(!r);

        IM3Function main2 = NULL;
        r                 = m3_FindFunction(&main2, rt2, "main");
        expect(!r);

        i32 value = 0;
        r         = m3_GetResultsV(main2, &value);
        expect(!r);
        expect(value == 436);

        m3_FreeRuntime(rt2);
        m3_FreeRuntime(rt1);
    }

    // a mutable externref global "host", and "main", which counts to 1000 in
    // a loop and returns the count
    static const u8 c_externWasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03,
        0x02, 0x01, 0x00, 0x06, 0x06, 0x01, 0x6f, 0x01,
        0xd0, 0x6f, 0x0b, 0x07, 0x0f, 0x02, 0x04, 0x68,
        0x6f, 0x73, 0x74, 0x03, 0x00, 0x04, 0x6d, 0x61,
        0x69, 0x6e, 0x00, 0x00, 0x0a, 0x1a, 0x01, 0x18,
        0x01, 0x01, 0x7f, 0x03, 0x40, 0x20, 0x00, 0x41,
        0x01, 0x6a, 0x21, 0x00, 0x20, 0x00, 0x41, 0xe8,
        0x07, 0x49, 0x0d, 0x00, 0x0b, 0x20, 0x00, 0x0b
    };

    // An externref is the host's to name, and a snapshot has no way to: one
    // that is not null refuses the save, where a null one does not
    Test(snapshot.externref_is_refused)
    {
        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(runtime != NULL);
        m3_SetSuspendable(runtime, true);

        IM3Module module = NULL;
        M3Result  r      = m3_ParseModule(env, &module, c_externWasm, sizeof(c_externWasm));
        expect(!r);
        r = m3_LoadModule(runtime, module);
        expect(!r);

        IM3Function function = NULL;
        r                    = m3_FindFunction(&function, runtime, "main");
        expect(!r);

        m3_RequestSuspend(runtime);
        r = m3_CallV(function);
        expect(r == m3Err_continuationSuspended);

        IM3Global host = m3_FindGlobal(module, "host");
        expect(host != NULL);

        void*  bytes = NULL;
        size_t size  = 0;

        if (host) {
            static int c_hostObject;

            host->refValue = &c_hostObject;
            r              = m3_SaveSnapshotToBuffer(runtime, &bytes, &size);
            expect(r and !strcmp(r, "an externref belongs to the host, and cannot be saved"));
            expect(bytes == NULL);

            host->refValue = NULL;
            r              = m3_SaveSnapshotToBuffer(runtime, &bytes, &size);
            expect(!r);
            free(bytes);
        }

        m3_FreeRuntime(runtime);
    }

    // With hooks, the embedder names an externref on the way out and binds the
    // name to a reference of its own on the way in, and carries state of its
    // own along with the program's. A runtime without them refuses a snapshot
    // that needs them.
    Test(snapshot.hooks_carry_host_references_and_state)
    {
        static int c_before, c_after;

        M3SnapshotHooks hooks;
        memset(&hooks, 0, sizeof(hooks));
        hooks.nameExternRef = SnapshotTest_NameExternRef;
        hooks.bindExternRef = SnapshotTest_BindExternRef;
        hooks.saveHostState = SnapshotTest_SaveHostState;
        hooks.loadHostState = SnapshotTest_LoadHostState;

        g_snapshotTestBefore = &c_before;
        g_snapshotTestAfter  = &c_after;

        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);
        m3_SetSnapshotHooks(rt1, &hooks);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_externWasm, sizeof(c_externWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function main1 = NULL;
        r                 = m3_FindFunction(&main1, rt1, "main");
        expect(!r);

        m3_RequestSuspend(rt1);
        r = m3_CallV(main1);
        expect(r == m3Err_continuationSuspended);

        IM3Global host1 = m3_FindGlobal(mod1, "host");
        expect(host1 != NULL);
        if (host1) {
            host1->refValue = &c_before;
        }

        void*  bytes = NULL;
        size_t size  = 0;
        r            = m3_SaveSnapshotToBuffer(rt1, &bytes, &size);
        expect(!r);

        // No hooks, normal hooks, malformed globals, a host that reads less
        // than its state, and a host callback that ignores an overread error.
        for (u32 withHooks = 0; withHooks < 5; ++withHooks) {
            IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
            expect(rt2 != NULL);
            if (withHooks) {
                m3_SetSnapshotHooks(rt2, &hooks);
            }

            IM3Module mod2 = NULL;
            r              = m3_ParseModule(env, &mod2, c_externWasm, sizeof(c_externWasm));
            expect(!r);
            r = m3_LoadModule(rt2, mod2);
            expect(!r);

            g_snapshotTestHostState[0] = 0;
            g_snapshotTestBindings     = 0;
            g_snapshotTestHostLoads    = 0;
            g_snapshotTestReadPastEnd  = (withHooks == 4);
            g_snapshotTestReadShort    = (withHooks == 3);

            void* malformed = NULL;
            if (withHooks == 2) {
                malformed = SnapshotTest_AppendSectionByte(bytes, size, 3);
                expect(malformed != NULL);
            }
            r = m3_LoadSnapshotFromBuffer(rt2, mod2, malformed ? malformed : bytes, size + (malformed ? 1 : 0));
            free(malformed);

            if (not withHooks) {
                expect(r and !strcmp(r, "the snapshot holds an externref, and nothing here can bind one"));
            } else if (withHooks >= 2) {
                expect(r != NULL);
                expect(g_snapshotTestHostLoads == (withHooks >= 3 ? 1 : 0));
                expect(g_snapshotTestBindings == (withHooks == 2 ? 0 : 1));
                if (withHooks == 2) {
                    IM3Global host2 = m3_FindGlobal(mod2, "host");
                    expect(host2 != NULL and host2->refValue == NULL);
                }
            } else {
                expect(!r);
                expect(g_snapshotTestBindings == 1 and g_snapshotTestHostLoads == 1);
                expect(!strcmp(g_snapshotTestHostState, "the host's own"));

                IM3Global host2 = m3_FindGlobal(mod2, "host");
                expect(host2 != NULL and host2->refValue == &c_after);

                r = m3_ResumeRuntime(rt2);
                expect(!r);

                IM3Function main2 = NULL;
                r                 = m3_FindFunction(&main2, rt2, "main");
                expect(!r);

                i32 value = 0;
                r         = m3_GetResultsV(main2, &value);
                expect(!r and value == 1000);
            }

            m3_FreeRuntime(rt2);
        }

        g_snapshotTestReadShort = false;

        free(bytes);
        m3_FreeRuntime(rt1);
    }

    // Every number in a snapshot is little endian, whatever the host is, and
    // The fixed header identifies the container; Meta carries the timestamp
    // and module hash as LEB128 values, without an engine fingerprint.
    Test(snapshot.header_is_portable)
    {
        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(runtime != NULL);
        m3_SetSuspendable(runtime, true);

        IM3Module module = NULL;
        M3Result  r      = m3_ParseModule(env, &module, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(runtime, module);
        expect(!r);

        IM3Function run = NULL;
        r               = m3_FindFunction(&run, runtime, "run");
        expect(!r);

        m3_RequestSuspend(runtime);
        r = m3_CallV(run);
        expect(r == m3Err_continuationSuspended);

        void*  bytes = NULL;
        size_t size  = 0;
        r            = m3_SaveSnapshotToBuffer(runtime, &bytes, &size);
        expect(!r);
        expect(size > 16);

        if (bytes and size > 16) {
            const u8* b = (const u8*)bytes;
            expect(memcmp(b, "\0dmp", 4) == 0);
            expect(b[4] == 1 and b[5] == 0 and b[6] == 0 and b[7] == 0); // version, 4 bytes
            expect(b[8] == 0);                                           // section 0 (Meta)
            u32 pos = 9;                                                 // past the section's size
            while (b[pos++] & 0x80) {
            }
            u32 flags = b[pos++];
            expect(flags == 0);

            u64 timestamp = 0;
            u32 shift     = 0;
            for (;;) {
                u8 byte = b[pos++];
                timestamp |= (u64)(byte & 0x7F) << shift;
                if (!(byte & 0x80)) {
                    break;
                }
                shift += 7;
            }
            expect(timestamp > 1700000000000ULL);
        }

        free(bytes);
        m3_FreeRuntime(runtime);
    }

    // A snapshot names the module it was taken from, and does not go into another
    Test(snapshot.refuses_another_module)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function func1 = NULL;
        r                 = m3_FindFunction(&func1, rt1, "run");
        expect(!r);

        m3_RequestSuspend(rt1);
        r = m3_CallV(func1);
        expect(r == m3Err_continuationSuspended);

        void*  bytes = NULL;
        size_t size  = 0;
        r            = m3_SaveSnapshotToBuffer(rt1, &bytes, &size);
        expect(!r);
        m3_FreeRuntime(rt1);

        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_snapshotFuncrefsWasm, sizeof(c_snapshotFuncrefsWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, bytes, size);
        expect(r and !strcmp(r, "the snapshot was saved from a different module"));
        expect(!m3_IsSuspended(rt2));

        free(bytes);
        m3_FreeRuntime(rt2);
    }

    Test(snapshot.embedded_roundtrip)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_snapshotBigFrameWasm, sizeof(c_snapshotBigFrameWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function func1 = NULL;
        r                 = m3_FindFunction(&func1, rt1, "run");
        expect(!r);

        m3_RequestSuspend(rt1);
        r = m3_Call(func1, 0, NULL);
        expect(r == m3Err_continuationSuspended);
        expect(m3_IsSuspended(rt1));

        void*  wasmWithSnap = NULL;
        size_t wasmSize     = 0;
        r                   = m3_SaveSnapshotToModule(rt1, mod1, "checkpoint1", &wasmWithSnap, &wasmSize);
        expect(!r);
        expect(wasmWithSnap != NULL && wasmSize > sizeof(c_snapshotBigFrameWasm));

        m3_FreeRuntime(rt1);

        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);
        m3_SetSuspendable(rt2, true);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, (const u8*)wasmWithSnap, (u32)wasmSize);
        expect(!r);
        expect(m3_HasSnapshot(mod2, "checkpoint1"));
        expect(!m3_HasSnapshot(mod2, "nonexistent"));

        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_CompileModule(mod2);
        expect(!r);

        r = m3_LoadEmbeddedSnapshot(rt2, mod2, "checkpoint1");
        expect(!r);
        expect(m3_IsSuspended(rt2));

        r = m3_ResumeRuntime(rt2);
        expect(!r);
        expect(!m3_IsSuspended(rt2));

        free(wasmWithSnap);
        m3_FreeRuntime(rt2);
    }

    // Nothing paused and nothing called: there is no module to describe
    Test(snapshot.nothing_to_save)
    {
        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(runtime != NULL);

        void*    bytes = NULL;
        size_t   size  = 0;
        M3Result r     = m3_SaveSnapshotToBuffer(runtime, &bytes, &size);
        expect(r and !strcmp(r, "there is nothing to snapshot"));
        expect(bytes == NULL and size == 0);

        m3_FreeRuntime(runtime);
    }

    // An embedded snapshot is one to resume, and the unnamed one resumes just
    // by running the module: a postmortem would leave a module that cannot run
    Test(snapshot.postmortem_is_not_embedded)
    {
        IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(runtime != NULL);

        IM3Module module = NULL;
        M3Result  r      = m3_ParseModule(env, &module, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(runtime, module);
        expect(!r);

        IM3Function run = NULL;
        r               = m3_FindFunction(&run, runtime, "run");
        expect(!r);
        r = m3_CallV(run);
        expect(!r);

        void*  bytes = NULL;
        size_t size  = 0;
        r            = m3_SaveSnapshotToModule(runtime, module, NULL, &bytes, &size);
        expect(r and !strcmp(r, "a postmortem cannot be embedded in a module"));
        expect(bytes == NULL);

        m3_FreeRuntime(runtime);
    }

    // A custom section does not decide whether a module is valid: two
    // snapshots of one name parse, and only selecting that name fails
    Test(snapshot.duplicate_name_fails_when_selected)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function run = NULL;
        r               = m3_FindFunction(&run, rt1, "run");
        expect(!r);

        m3_RequestSuspend(rt1);
        r = m3_CallV(run);
        expect(r == m3Err_continuationSuspended);

        void*  once     = NULL;
        size_t onceSize = 0;
        r               = m3_SaveSnapshotToModule(rt1, mod1, "cp", &once, &onceSize);
        expect(!r);
        m3_FreeRuntime(rt1);

        // the new section went on the end, so repeating the tail repeats it
        size_t sectionSize = onceSize - sizeof(c_loopCounterWasm);
        u8*    twice       = (u8*)malloc(onceSize + sectionSize);
        expect(twice != NULL);

        if (once and twice) {
            memcpy(twice, once, onceSize);
            memcpy(twice + onceSize, (u8*)once + sizeof(c_loopCounterWasm), sectionSize);

            IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
            expect(rt2 != NULL);
            m3_SetSuspendable(rt2, true);

            IM3Module mod2 = NULL;
            r              = m3_ParseModule(env, &mod2, twice, (u32)(onceSize + sectionSize));
            expect(!r);
            r = m3_LoadModule(rt2, mod2);
            expect(!r);

            expect(m3_HasSnapshot(mod2, "cp"));

            const void* data     = NULL;
            size_t      dataSize = 0;
            r                    = m3_GetEmbeddedSnapshot(mod2, "cp", &data, &dataSize);
            expect(r and !strcmp(r, "duplicate embedded snapshot name"));

            r = m3_LoadEmbeddedSnapshot(rt2, mod2, "cp");
            expect(r and !strcmp(r, "duplicate embedded snapshot name"));

            // nothing was restored, so the module still runs cold
            r = m3_FindFunction(&run, rt2, "run");
            expect(!r);
            r = m3_CallV(run);
            expect(!r);

            m3_FreeRuntime(rt2);
        }

        free(twice);
        free(once);
    }

    // A restore that fails once part of the snapshot is in leaves a module in
    // a state the program was never in: it refuses to run, or to take another
    Test(snapshot.failed_restore_leaves_module_unusable)
    {
        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function run = NULL;
        r               = m3_FindFunction(&run, rt1, "run");
        expect(!r);

        m3_RequestSuspend(rt1);
        r = m3_CallV(run);
        expect(r == m3Err_continuationSuspended);

        void*  bytes = NULL;
        size_t size  = 0;
        r            = m3_SaveSnapshotToBuffer(rt1, &bytes, &size);
        expect(!r);
        m3_FreeRuntime(rt1);

        // the Continuation section comes after Memory and Global, which are
        // restored by the time it is found malformed
        void* malformed = SnapshotTest_AppendSectionByte(bytes, size, 6);
        expect(malformed != NULL);

        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_loopCounterWasm, sizeof(c_loopCounterWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, malformed, size + 1);
        expect(r != NULL);

        r = m3_FindFunction(&run, rt2, "run");
        expect(!r);
        r = m3_CallV(run);
        expect(r and !strcmp(r, "a snapshot failed to restore into the module, so it cannot run"));

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, bytes, size);
        expect(r and !strcmp(r, "a snapshot failed to restore into the module, so it cannot be used"));

        free(malformed);
        free(bytes);
        m3_FreeRuntime(rt2);
    }


    Test(snapshot.float_frame_values_round_trip)
    {
        // f64 and f32 locals held across a loop back edge. Both are written as
        // bit patterns rather than as the bytes a slot happens to hold, so a
        // resumed run has to see the very values the paused one did.
        const double c_pi    = 3.141592653589793;
        const float  c_small = 1234.5678f;

        IM3Runtime rt1 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt1 != NULL);
        m3_SetSuspendable(rt1, true);

        IM3Module mod1 = NULL;
        M3Result  r    = m3_ParseModule(env, &mod1, c_snapshotFloatsWasm, sizeof(c_snapshotFloatsWasm));
        expect(!r);
        r = m3_LoadModule(rt1, mod1);
        expect(!r);

        IM3Function run = NULL;
        r               = m3_FindFunction(&run, rt1, "run");
        expect(!r);

        m3_RequestSuspend(rt1);
        r = m3_Call(run, 0, NULL);
        expect(r == m3Err_continuationSuspended);

        void*  bytes = NULL;
        size_t size  = 0;
        r            = m3_SaveSnapshotToBuffer(rt1, &bytes, &size);
        expect(!r);

        m3_FreeRuntime(rt1);

        IM3Runtime rt2 = m3_NewRuntime(env, 64 * 1024, NULL);
        expect(rt2 != NULL);
        m3_SetSuspendable(rt2, true);

        IM3Module mod2 = NULL;
        r              = m3_ParseModule(env, &mod2, c_snapshotFloatsWasm, sizeof(c_snapshotFloatsWasm));
        expect(!r);
        r = m3_LoadModule(rt2, mod2);
        expect(!r);
        r = m3_CompileModule(mod2);
        expect(!r);

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, bytes, size);
        expect(!r);

        r = m3_ResumeRuntime(rt2);
        expect(!r);

        M3TaggedValue d, f;
        r = m3_GetGlobal(m3_FindGlobal(mod2, "d"), &d);
        expect(!r);
        r = m3_GetGlobal(m3_FindGlobal(mod2, "f"), &f);
        expect(!r);

        expect(d.type == c_m3Type_f64 and d.value.f64 == c_pi);
        expect(f.type == c_m3Type_f32 and f.value.f32 == c_small);

        free(bytes);
        m3_FreeRuntime(rt2);
    }

    Test(snapshot.embedded_save_replaces_the_one_it_finds)
    {
        // Checkpointing the same module over and over must not stack one
        // snapshot section on another: the binary would grow without end.
        void*  wasm         = NULL;
        size_t size         = 0;
        size_t previousSize = 0;

        for (u32 round = 0; round < 3; ++round) {
            IM3Runtime rt = m3_NewRuntime(env, 64 * 1024, NULL);
            expect(rt != NULL);
            m3_SetSuspendable(rt, true);

            IM3Module mod = NULL;
            M3Result  r   = wasm ? m3_ParseModule(env, &mod, (const u8*)wasm, (u32)size)
                                 : m3_ParseModule(env, &mod, c_snapshotBigFrameWasm, sizeof(c_snapshotBigFrameWasm));
            expect(!r);
            r = m3_LoadModule(rt, mod);
            expect(!r);

            IM3Function run = NULL;
            r               = m3_FindFunction(&run, rt, "run");
            expect(!r);

            m3_RequestSuspend(rt);
            r = m3_Call(run, 0, NULL);
            expect(r == m3Err_continuationSuspended);

            {
                void*  next     = NULL;
                size_t nextSize = 0;

                r = m3_SaveSnapshotToModule(rt, mod, "cp", &next, &nextSize);
                expect(!r);

                free(wasm);
                wasm = next;
                size = nextSize;
            }

            m3_FreeRuntime(rt);

            // the first round adds the section; every one after it replaces the
            // section it finds, so the size settles
            if (round == 1) {
                previousSize = size;
            } else if (round > 1) {
                expect(size == previousSize);
            }
        }

        {
            IM3Module mod = NULL;
            M3Result  r   = m3_ParseModule(env, &mod, (const u8*)wasm, (u32)size);
            expect(!r);
            expect(m3_HasSnapshot(mod, "cp"));
            expect(!m3_HasSnapshot(mod, ""));
            m3_FreeModule(mod);
        }

        free(wasm);
    }


#endif // d_m3HasSnapshots


    m3_FreeEnvironment(env);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
