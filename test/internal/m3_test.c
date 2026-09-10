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

    m3_FreeEnvironment(env);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
