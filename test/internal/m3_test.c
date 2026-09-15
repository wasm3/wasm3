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


int main (int argc, const char* argv[])
{
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
        uintptr_t small = (uintptr_t)m3_NativeStackLimit(frame, 4096);

        expect(small == sp - 4096)

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
            u32 flags = 0;
            memcpy(&flags, (u8*)bytes + 4, sizeof(flags));
            expect(memcmp(bytes, "W3S\x01", 4) == 0);
            expect(flags == 1);
        }

        r = m3_LoadSnapshotFromBuffer(runtime, module, bytes, size);
        expect(r && !strcmp(r, "postmortem snapshots cannot be resumed"));

        free(bytes);
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
        expect(snapBytes != NULL && snapSize > 0);
        expect(snapSize >= 4);
        expect(memcmp(snapBytes, "W3S\x01", 4) == 0);

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

        r = m3_LoadSnapshotFromBuffer(rt2, mod2, postmortemBytes, postmortemSize);
        expect(r && !strcmp(r, "postmortem snapshots cannot be resumed"));

        free(snapBytes);
        free(postmortemBytes);
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
        m3_SetGasLimit(rt1, 100);
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
        m3_SetGasLimit(rt2, 10000000);

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

#endif // d_m3HasSnapshots


    m3_FreeEnvironment(env);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
