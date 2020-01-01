#include <stdio.h>
#include <assert.h>
#include <time.h>

#include "wasm_api.h"

/*
 * Result: "Raw" calls are ~2x faster than native arg "pushers".
 *
 * WARNING: this benchmark no longer works.
 * Native calls were removed along with wasm3_native_sum.
 * It may be useful in future when we implement libffi calls, etc.
 */

static inline
double get_time() {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int main(int argc, char **argv)
{
  const unsigned cycles = (argc > 1) ? atol(argv[1]) : 10000000;
  double beg, end;

  printf("Validation...\n");
  fflush(stdout);

  // validate
  assert(wasm3_raw_sum(10, 20, 30, 40)    == 10 + 20 + 30 + 40);
  assert(wasm3_native_sum(10, 20, 30, 40) == 10 + 20 + 30 + 40);

  printf("Warm-up...\n");
  fflush(stdout);

  beg = get_time();
  for (unsigned i = 0; i < cycles/10; i++) {
      wasm3_raw_sum(10, 20, 30, 40);
      wasm3_native_sum(10, 20, 30, 40);
  }

  printf("Running test...\n");
  fflush(stdout);

  // actual test
  beg = get_time();
  for (unsigned i = 0; i < cycles; i++) {
      wasm3_raw_sum(1, 2, 3, 4);
  }
  end = get_time();
  const double time_raw = (end - beg);

  beg = get_time();
  for (unsigned i = 0; i < cycles; i++) {
      wasm3_native_sum(1, 2, 3, 4);
  }
  end = get_time();
  const double time_native = (end - beg);

  printf("Native: %.3f ms\n", time_native);
  printf("Raw:    %.3f ms\n", time_raw);

  printf("Native/Raw: %.3f\n", time_native/time_raw);
  return 0;
}
