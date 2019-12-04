#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/*
 * Helpers
 */

#define WASM_EXPORT __attribute__((used)) __attribute__((visibility ("default")))

static inline
struct timespec timespec_diff(struct timespec start, struct timespec end)
{
  struct timespec temp;
  if ((end.tv_nsec-start.tv_nsec)<0) {
    temp.tv_sec = end.tv_sec-start.tv_sec-1;
    temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
  } else {
    temp.tv_sec = end.tv_sec-start.tv_sec;
    temp.tv_nsec = end.tv_nsec-start.tv_nsec;
  }
  return temp;
}

static inline
int rand_range(int min, int max){
   return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

WASM_EXPORT
uint32_t fib(uint32_t n)
{
  if(n < 2) {
    return n;
  }
  return fib(n-1) + fib(n-2);
}

/*
 * Tests
 */

static char gString[16];

__attribute__((constructor))
void test_init_some_global() {
  static const char data[] = "Constructor OK\n";
  memcpy(gString, data, sizeof(data));
}

void test_constructor() {
  fwrite(gString, 1, sizeof(gString), stdout);
}

void test_write() {
  fwrite("Hello world\n", 1, 12, stdout);
}

void test_printf() {
  printf("Hello %s!\n", "printf");
}

void test_random() {
  unsigned entropy;
  getentropy(&entropy, sizeof(entropy));
  srand(entropy);
  int x = rand_range(0, 10);
  int y = rand_range(0, 10);
  printf("%d + %d = %d\n", x, y, x+y);
}

void test_gettime() {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  printf("Now: %lld sec, %ld ns\n", now.tv_sec, now.tv_nsec);
}

void test_fib10() {
  volatile uint32_t n = 10, result;
  result = fib(n);
  printf("fib(%d) = %d\n", n, result);
}

void test_perf_fib38() {
  struct timespec start, finish;
  volatile uint32_t n = 38, result;

  printf("fib(%d) = ", n);
  fflush(stdout);

  // Chew up some CPU time
  clock_gettime(CLOCK_REALTIME, &start);
  result = fib(n);
  clock_gettime(CLOCK_REALTIME, &finish);

  struct timespec delta = timespec_diff(start, finish);
  unsigned ms = (delta.tv_sec*1000) + (delta.tv_nsec/1000000);
  printf("%d [%u ms]\n", result, ms);
}

/*
 * Main
 */

int main()
{
  test_write();
  test_constructor();
  test_printf();
  test_gettime();
  //test_random();
  test_fib10();
  test_perf_fib38();
  return 0;
}
