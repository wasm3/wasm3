/*
 * WASI clock_time_get and clock_res_get implementation
 *
 * Provides POSIX clock mapping for WASI clock IDs with platform abstraction
 * for Windows, Linux, and macOS.
 *
 * SPDX-License-Identifier: MIT
 */

#include "m3_api_wasi.h"
#include "m3_env.h"
#include "m3_exception.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* WASI clock IDs as defined in wasi_snapshot_preview1 */
#define WASI_CLOCK_REALTIME           0
#define WASI_CLOCK_MONOTONIC          1
#define WASI_CLOCK_PROCESS_CPUTIME_ID 2
#define WASI_CLOCK_THREAD_CPUTIME_ID  3

/* WASI errno values */
#define WASI_ERRNO_SUCCESS    0
#define WASI_ERRNO_INVAL     28
#define WASI_ERRNO_NOTSUP    58
#define WASI_ERRNO_OVERFLOW  61

/* Nanosecond conversion constants */
#define NANOS_PER_SEC   1000000000ULL
#define NANOS_PER_MILLI 1000000ULL
#define NANOS_PER_MICRO 1000ULL

/* ---- Platform abstraction layer ---- */

#if defined(_WIN32) || defined(_WIN64)
/* ======================= Windows Implementation ======================= */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static uint64_t wasi_clock_perf_frequency = 0;

static void wasi_clock_init_frequency(void)
{
    if (wasi_clock_perf_frequency == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        wasi_clock_perf_frequency = (uint64_t)freq.QuadPart;
    }
}

static int wasi_clock_get_realtime(uint64_t *timestamp_ns)
{
    FILETIME ft;
    ULARGE_INTEGER uli;

    GetSystemTimePreciseAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    /* Convert Windows FILETIME (100-ns intervals since 1601-01-01)
     * to Unix epoch nanoseconds (since 1970-01-01) */
    static const uint64_t EPOCH_DIFF = 116444736000000000ULL;
    if (uli.QuadPart < EPOCH_DIFF) {
        *timestamp_ns = 0;
        return WASI_ERRNO_SUCCESS;
    }
    *timestamp_ns = (uli.QuadPart - EPOCH_DIFF) * 100ULL;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_monotonic(uint64_t *timestamp_ns)
{
    LARGE_INTEGER counter;

    wasi_clock_init_frequency();
    QueryPerformanceCounter(&counter);

    uint64_t sec = (uint64_t)counter.QuadPart / wasi_clock_perf_frequency;
    uint64_t rem = (uint64_t)counter.QuadPart % wasi_clock_perf_frequency;
    *timestamp_ns = sec * NANOS_PER_SEC + (rem * NANOS_PER_SEC) / wasi_clock_perf_frequency;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_process_cputime(uint64_t *timestamp_ns)
{
    FILETIME creation, exit_time, kernel, user;
    ULARGE_INTEGER k, u;

    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user)) {
        return WASI_ERRNO_NOTSUP;
    }
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;

    /* FILETIME units are 100-nanosecond intervals */
    *timestamp_ns = (k.QuadPart + u.QuadPart) * 100ULL;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_thread_cputime(uint64_t *timestamp_ns)
{
    FILETIME creation, exit_time, kernel, user;
    ULARGE_INTEGER k, u;

    if (!GetThreadTimes(GetCurrentThread(), &creation, &exit_time, &kernel, &user)) {
        return WASI_ERRNO_NOTSUP;
    }
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;

    *timestamp_ns = (k.QuadPart + u.QuadPart) * 100ULL;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_realtime_res(uint64_t *resolution_ns)
{
    /* GetSystemTimePreciseAsFileTime has ~100ns resolution on modern Windows */
    *resolution_ns = 100ULL;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_monotonic_res(uint64_t *resolution_ns)
{
    wasi_clock_init_frequency();
    if (wasi_clock_perf_frequency == 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *resolution_ns = NANOS_PER_SEC / wasi_clock_perf_frequency;
    if (*resolution_ns == 0) {
        *resolution_ns = 1;
    }
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_process_cputime_res(uint64_t *resolution_ns)
{
    /* Windows process times have 100ns granularity */
    *resolution_ns = 100ULL;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_thread_cputime_res(uint64_t *resolution_ns)
{
    /* Windows thread times have 100ns granularity */
    *resolution_ns = 100ULL;
    return WASI_ERRNO_SUCCESS;
}

#elif defined(__APPLE__)
/* ======================= macOS Implementation ======================= */

#include <mach/mach_time.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

static mach_timebase_info_data_t wasi_clock_timebase = {0, 0};

static void wasi_clock_init_timebase(void)
{
    if (wasi_clock_timebase.denom == 0) {
        mach_timebase_info(&wasi_clock_timebase);
    }
}

static int wasi_clock_get_realtime(uint64_t *timestamp_ns)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *timestamp_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_monotonic(uint64_t *timestamp_ns)
{
    uint64_t mach_time = mach_absolute_time();
    wasi_clock_init_timebase();
    *timestamp_ns = mach_time * wasi_clock_timebase.numer / wasi_clock_timebase.denom;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_process_cputime(uint64_t *timestamp_ns)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) {
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru) != 0) {
            return WASI_ERRNO_NOTSUP;
        }
        uint64_t user_ns = (uint64_t)ru.ru_utime.tv_sec * NANOS_PER_SEC
                         + (uint64_t)ru.ru_utime.tv_usec * NANOS_PER_MICRO;
        uint64_t sys_ns = (uint64_t)ru.ru_stime.tv_sec * NANOS_PER_SEC
                        + (uint64_t)ru.ru_stime.tv_usec * NANOS_PER_MICRO;
        *timestamp_ns = user_ns + sys_ns;
        return WASI_ERRNO_SUCCESS;
    }
    *timestamp_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_thread_cputime(uint64_t *timestamp_ns)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *timestamp_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_realtime_res(uint64_t *resolution_ns)
{
    struct timespec ts;
    if (clock_getres(CLOCK_REALTIME, &ts) != 0) {
        *resolution_ns = NANOS_PER_MICRO; /* fallback: 1 microsecond */
        return WASI_ERRNO_SUCCESS;
    }
    *resolution_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_monotonic_res(uint64_t *resolution_ns)
{
    wasi_clock_init_timebase();
    *resolution_ns = wasi_clock_timebase.numer / wasi_clock_timebase.denom;
    if (*resolution_ns == 0) {
        *resolution_ns = 1;
    }
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_process_cputime_res(uint64_t *resolution_ns)
{
    struct timespec ts;
    if (clock_getres(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) {
        *resolution_ns = NANOS_PER_MICRO;
        return WASI_ERRNO_SUCCESS;
    }
    *resolution_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_thread_cputime_res(uint64_t *resolution_ns)
{
    struct timespec ts;
    if (clock_getres(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        *resolution_ns = NANOS_PER_MICRO;
        return WASI_ERRNO_SUCCESS;
    }
    *resolution_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

#else
/* ======================= Linux / POSIX Implementation ======================= */

#include <time.h>
#include <errno.h>

static int posix_clockid_from_wasi(uint32_t wasi_clock_id, clockid_t *posix_id)
{
    switch (wasi_clock_id) {
        case WASI_CLOCK_REALTIME:
            *posix_id = CLOCK_REALTIME;
            return WASI_ERRNO_SUCCESS;
        case WASI_CLOCK_MONOTONIC:
            *posix_id = CLOCK_MONOTONIC;
            return WASI_ERRNO_SUCCESS;
        case WASI_CLOCK_PROCESS_CPUTIME_ID:
            *posix_id = CLOCK_PROCESS_CPUTIME_ID;
            return WASI_ERRNO_SUCCESS;
        case WASI_CLOCK_THREAD_CPUTIME_ID:
            *posix_id = CLOCK_THREAD_CPUTIME_ID;
            return WASI_ERRNO_SUCCESS;
        default:
            return WASI_ERRNO_INVAL;
    }
}

static int wasi_clock_get_realtime(uint64_t *timestamp_ns)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *timestamp_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_monotonic(uint64_t *timestamp_ns)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *timestamp_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_process_cputime(uint64_t *timestamp_ns)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *timestamp_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_thread_cputime(uint64_t *timestamp_ns)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *timestamp_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_realtime_res(uint64_t *resolution_ns)
{
    struct timespec ts;
    if (clock_getres(CLOCK_REALTIME, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *resolution_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_monotonic_res(uint64_t *resolution_ns)
{
    struct timespec ts;
    if (clock_getres(CLOCK_MONOTONIC, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *resolution_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_process_cputime_res(uint64_t *resolution_ns)
{
    struct timespec ts;
    if (clock_getres(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *resolution_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

static int wasi_clock_get_thread_cputime_res(uint64_t *resolution_ns)
{
    struct timespec ts;
    if (clock_getres(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return WASI_ERRNO_NOTSUP;
    }
    *resolution_ns = (uint64_t)ts.tv_sec * NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
    return WASI_ERRNO_SUCCESS;
}

#endif /* Platform selection */

/* ---- Unified WASI clock API ---- */

/*
 * wasi_clock_time_get - Get the time of a clock.
 *
 * @clock_id:  WASI clock identifier
 * @precision: Desired precision hint (not enforced, but may be used for coarse clocks)
 * @time_out:  Output timestamp in nanoseconds
 *
 * Returns WASI errno value.
 */
int wasi_clock_time_get(uint32_t clock_id, uint64_t precision, uint64_t *time_out)
{
    if (time_out == NULL) {
        return WASI_ERRNO_INVAL;
    }

    switch (clock_id) {
        case WASI_CLOCK_REALTIME:
            return wasi_clock_get_realtime(time_out);

        case WASI_CLOCK_MONOTONIC:
            return wasi_clock_get_monotonic(time_out);

        case WASI_CLOCK_PROCESS_CPUTIME_ID:
            return wasi_clock_get_process_cputime(time_out);

        case WASI_CLOCK_THREAD_CPUTIME_ID:
            return wasi_clock_get_thread_cputime(time_out);

        default:
            return WASI_ERRNO_INVAL;
    }
}

/*
 * wasi_clock_res_get - Get the resolution of a clock.
 *
 * @clock_id:       WASI clock identifier
 * @resolution_out: Output resolution in nanoseconds
 *
 * Returns WASI errno value.
 */
int wasi_clock_res_get(uint32_t clock_id, uint64_t *resolution_out)
{
    if (resolution_out == NULL) {
        return WASI_ERRNO_INVAL;
    }

    switch (clock_id) {
        case WASI_CLOCK_REALTIME:
            return wasi_clock_get_realtime_res(resolution_out);

        case WASI_CLOCK_MONOTONIC:
            return wasi_clock_get_monotonic_res(resolution_out);

        case WASI_CLOCK_PROCESS_CPUTIME_ID:
            return wasi_clock_get_process_cputime_res(resolution_out);

        case WASI_CLOCK_THREAD_CPUTIME_ID:
            return wasi_clock_get_thread_cputime_res(resolution_out);

        default:
            return WASI_ERRNO_INVAL;
    }
}

/*
 * wasi_clock_validate_id - Check whether a clock ID is supported.
 *
 * @clock_id: WASI clock identifier
 *
 * Returns 1 if valid, 0 otherwise.
 */
int wasi_clock_validate_id(uint32_t clock_id)
{
    return (clock_id <= WASI_CLOCK_THREAD_CPUTIME_ID) ? 1 : 0;
}

/*
 * wasi_clock_monotonic_elapsed - Compute elapsed nanoseconds between two
 *                                monotonic timestamps.
 *
 * @start_ns: Start timestamp in nanoseconds
 * @end_ns:   End timestamp in nanoseconds
 * @elapsed:  Output elapsed nanoseconds
 *
 * Returns WASI errno value. Returns INVAL if end < start.
 */
int wasi_clock_monotonic_elapsed(uint64_t start_ns, uint64_t end_ns, uint64_t *elapsed)
{
    if (elapsed == NULL) {
        return WASI_ERRNO_INVAL;
    }
    if (end_ns < start_ns) {
        return WASI_ERRNO_INVAL;
    }
    *elapsed = end_ns - start_ns;
    return WASI_ERRNO_SUCCESS;
}

/*
 * wasi_clock_realtime_to_components - Break a realtime timestamp into
 *                                     seconds and nanoseconds remainder.
 *
 * @timestamp_ns: Input timestamp in nanoseconds
 * @seconds:      Output whole seconds since Unix epoch
 * @nanos:        Output remaining nanoseconds
 *
 * Returns WASI errno value.
 */
int wasi_clock_realtime_to_components(uint64_t timestamp_ns,
                                      uint64_t *seconds,
                                      uint64_t *nanos)
{
    if (seconds == NULL || nanos == NULL) {
        return WASI_ERRNO_INVAL;
    }
    *seconds = timestamp_ns / NANOS_PER_SEC;
    *nanos   = timestamp_ns % NANOS_PER_SEC;
    return WASI_ERRNO_SUCCESS;
}

/*
 * wasi_clock_timestamp_from_components - Build a nanosecond timestamp from
 *                                        seconds + nanoseconds.
 *
 * @seconds:      Whole seconds
 * @nanos:        Nanosecond remainder (must be < 1e9)
 * @timestamp_ns: Output timestamp
 *
 * Returns WASI errno value. Returns INVAL if nanos >= 1e9.
 * Returns OVERFLOW if the result would overflow uint64_t.
 */
int wasi_clock_timestamp_from_components(uint64_t seconds,
                                         uint64_t nanos,
                                         uint64_t *timestamp_ns)
{
    if (timestamp_ns == NULL) {
        return WASI_ERRNO_INVAL;
    }
    if (nanos >= NANOS_PER_SEC) {
        return WASI_ERRNO_INVAL;
    }
    /* Check for overflow */
    if (seconds > (UINT64_MAX - nanos) / NANOS_PER_SEC) {
        return WASI_ERRNO_OVERFLOW;
    }
    *timestamp_ns = seconds * NANOS_PER_SEC + nanos;
    return WASI_ERRNO_SUCCESS;
}

#if defined(__cplusplus)
}
#endif
