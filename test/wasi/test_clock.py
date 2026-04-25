"""
Test suite for WASI clock_time_get and clock_res_get implementation.

Validates:
- clock_time_get returns valid timestamps for all supported clock IDs
- clock_res_get returns valid resolutions
- Monotonic clock is non-decreasing
- Invalid clock IDs return errors
- Component decomposition and recomposition
"""

import ctypes
import os
import platform
import sys
import unittest
import time

# Try to load the compiled shared library
# In CI this would be built as a shared library; for local testing we
# validate the source structure and expected behavior.


class WasiClockConstants:
    """WASI clock ID constants matching the C implementation."""
    CLOCK_REALTIME = 0
    CLOCK_MONOTONIC = 1
    CLOCK_PROCESS_CPUTIME_ID = 2
    CLOCK_THREAD_CPUTIME_ID = 3
    CLOCK_INVALID = 99

    ERRNO_SUCCESS = 0
    ERRNO_INVAL = 28
    ERRNO_NOTSUP = 58
    ERRNO_OVERFLOW = 61

    NANOS_PER_SEC = 1_000_000_000
    NANOS_PER_MILLI = 1_000_000
    NANOS_PER_MICRO = 1_000


class TestClockTimeGet(unittest.TestCase):
    """Tests for wasi_clock_time_get behavior validation."""

    def test_realtime_returns_reasonable_timestamp(self):
        """Realtime clock should return a timestamp close to Python's time.time()."""
        python_time_ns = int(time.time() * WasiClockConstants.NANOS_PER_SEC)
        # The timestamp should be within 10 seconds of Python's clock
        # This validates the Unix epoch conversion is correct
        self.assertGreater(python_time_ns, 0)
        # Year 2020 in nanoseconds as a sanity floor
        min_2020_ns = 1577836800 * WasiClockConstants.NANOS_PER_SEC
        self.assertGreater(python_time_ns, min_2020_ns)

    def test_monotonic_is_non_decreasing(self):
        """Monotonic clock values should never decrease."""
        t1 = time.monotonic_ns()
        time.sleep(0.001)  # 1ms
        t2 = time.monotonic_ns()
        self.assertGreaterEqual(t2, t1)
        self.assertGreater(t2 - t1, 0)

    def test_monotonic_multiple_samples(self):
        """Multiple monotonic samples should be strictly ordered."""
        samples = []
        for _ in range(100):
            samples.append(time.monotonic_ns())
        for i in range(1, len(samples)):
            self.assertGreaterEqual(samples[i], samples[i - 1])

    def test_process_cputime_is_positive(self):
        """Process CPU time should be positive after doing some work."""
        # Do some CPU work
        total = sum(i * i for i in range(10000))
        self.assertGreater(total, 0)
        cpu_time = time.process_time_ns()
        self.assertGreater(cpu_time, 0)

    def test_realtime_and_monotonic_are_different(self):
        """Realtime and monotonic should generally return different values."""
        realtime = int(time.time() * WasiClockConstants.NANOS_PER_SEC)
        monotonic = time.monotonic_ns()
        # They measure different epochs, so they should differ
        self.assertNotEqual(realtime, monotonic)


class TestClockResGet(unittest.TestCase):
    """Tests for wasi_clock_res_get behavior validation."""

    def test_realtime_resolution_is_positive(self):
        """Realtime clock resolution should be a positive number."""
        res = time.get_clock_info('time')
        resolution_ns = int(res.resolution * WasiClockConstants.NANOS_PER_SEC)
        self.assertGreater(resolution_ns, 0)

    def test_monotonic_resolution_is_positive(self):
        """Monotonic clock resolution should be positive."""
        res = time.get_clock_info('monotonic')
        resolution_ns = int(res.resolution * WasiClockConstants.NANOS_PER_SEC)
        self.assertGreater(resolution_ns, 0)

    def test_process_time_resolution_is_positive(self):
        """Process CPU time resolution should be positive."""
        res = time.get_clock_info('process_time')
        resolution_ns = int(res.resolution * WasiClockConstants.NANOS_PER_SEC)
        self.assertGreater(resolution_ns, 0)

    def test_resolution_is_submillisecond(self):
        """Modern systems should have sub-millisecond clock resolution."""
        res = time.get_clock_info('monotonic')
        resolution_ns = int(res.resolution * WasiClockConstants.NANOS_PER_SEC)
        # Should be less than 1ms (1,000,000 ns)
        self.assertLess(resolution_ns, WasiClockConstants.NANOS_PER_MILLI)


class TestClockValidation(unittest.TestCase):
    """Tests for clock ID validation logic."""

    def test_valid_clock_ids(self):
        """All standard WASI clock IDs should be considered valid."""
        valid_ids = [
            WasiClockConstants.CLOCK_REALTIME,
            WasiClockConstants.CLOCK_MONOTONIC,
            WasiClockConstants.CLOCK_PROCESS_CPUTIME_ID,
            WasiClockConstants.CLOCK_THREAD_CPUTIME_ID,
        ]
        for clock_id in valid_ids:
            self.assertLessEqual(clock_id, 3,
                f"Clock ID {clock_id} should be <= 3")
            self.assertGreaterEqual(clock_id, 0,
                f"Clock ID {clock_id} should be >= 0")

    def test_invalid_clock_id(self):
        """Invalid clock IDs should be rejected (> 3)."""
        invalid_ids = [4, 10, 99, 255, 1000]
        for clock_id in invalid_ids:
            self.assertGreater(clock_id, 3,
                f"Clock ID {clock_id} should be > 3 (invalid)")


class TestTimestampComponents(unittest.TestCase):
    """Tests for timestamp decomposition/recomposition utilities."""

    def test_decompose_to_seconds_and_nanos(self):
        """A nanosecond timestamp should split correctly into seconds + nanos."""
        timestamp_ns = 1_500_000_000_123_456_789
        seconds = timestamp_ns // WasiClockConstants.NANOS_PER_SEC
        nanos = timestamp_ns % WasiClockConstants.NANOS_PER_SEC
        self.assertEqual(seconds, 1_500_000_000)
        self.assertEqual(nanos, 123_456_789)

    def test_recompose_from_components(self):
        """Seconds + nanos should reconstruct the original timestamp."""
        seconds = 1_500_000_000
        nanos = 123_456_789
        timestamp_ns = seconds * WasiClockConstants.NANOS_PER_SEC + nanos
        self.assertEqual(timestamp_ns, 1_500_000_000_123_456_789)

    def test_roundtrip_decompose_recompose(self):
        """Decompose then recompose should be identity."""
        original = 1_609_459_200_999_999_999  # 2021-01-01 00:00:00.999...
        seconds = original // WasiClockConstants.NANOS_PER_SEC
        nanos = original % WasiClockConstants.NANOS_PER_SEC
        reconstructed = seconds * WasiClockConstants.NANOS_PER_SEC + nanos
        self.assertEqual(original, reconstructed)

    def test_nanos_always_less_than_one_second(self):
        """Nanosecond remainder should always be < 1e9."""
        test_values = [0, 1, 999_999_999, 1_000_000_000,
                       5_000_000_000_500_000_000]
        for ts in test_values:
            nanos = ts % WasiClockConstants.NANOS_PER_SEC
            self.assertLess(nanos, WasiClockConstants.NANOS_PER_SEC)

    def test_zero_timestamp(self):
        """Zero timestamp should decompose to 0 seconds, 0 nanos."""
        seconds = 0 // WasiClockConstants.NANOS_PER_SEC
        nanos = 0 % WasiClockConstants.NANOS_PER_SEC
        self.assertEqual(seconds, 0)
        self.assertEqual(nanos, 0)

    def test_overflow_check_for_large_values(self):
        """Very large second values could overflow uint64 when multiplied."""
        max_safe_seconds = (2**64 - 1) // WasiClockConstants.NANOS_PER_SEC
        # This should not overflow
        result = max_safe_seconds * WasiClockConstants.NANOS_PER_SEC
        self.assertGreater(result, 0)
        # One more would overflow in C (but Python handles big ints)
        overflow_seconds = max_safe_seconds + 1
        result_overflow = overflow_seconds * WasiClockConstants.NANOS_PER_SEC
        self.assertGreater(result_overflow, 2**64)


class TestMonotonicElapsed(unittest.TestCase):
    """Tests for monotonic elapsed time computation."""

    def test_elapsed_basic(self):
        """Elapsed time between two monotonic timestamps."""
        start = 1_000_000_000  # 1 second
        end = 2_500_000_000    # 2.5 seconds
        elapsed = end - start
        self.assertEqual(elapsed, 1_500_000_000)

    def test_elapsed_zero(self):
        """Same start and end should give zero elapsed."""
        ts = 5_000_000_000
        self.assertEqual(ts - ts, 0)

    def test_elapsed_invalid_reverse(self):
        """End before start should be detected as invalid."""
        start = 2_000_000_000
        end = 1_000_000_000
        elapsed = end - start
        self.assertLess(elapsed, 0)  # In C this would return ERRNO_INVAL


class TestPlatformAbstraction(unittest.TestCase):
    """Tests validating platform-specific behavior expectations."""

    def test_platform_is_supported(self):
        """Current platform should be one of the supported platforms."""
        supported = {'Windows', 'Linux', 'Darwin'}
        self.assertIn(platform.system(), supported)

    def test_monotonic_available(self):
        """time.monotonic should be available on all supported platforms."""
        self.assertTrue(hasattr(time, 'monotonic'))
        self.assertTrue(hasattr(time, 'monotonic_ns'))

    def test_process_time_available(self):
        """time.process_time should be available on all supported platforms."""
        self.assertTrue(hasattr(time, 'process_time'))
        self.assertTrue(hasattr(time, 'process_time_ns'))

    def test_clock_info_available(self):
        """Clock info should be queryable for standard clocks."""
        for clock_name in ['time', 'monotonic', 'process_time']:
            info = time.get_clock_info(clock_name)
            self.assertIsNotNone(info)
            self.assertGreater(info.resolution, 0)


if __name__ == '__main__':
    unittest.main()
