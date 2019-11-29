#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define WASM_EXPORT __attribute__((used)) __attribute__((visibility ("default")))

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

WASM_EXPORT
uint32_t fib(uint32_t n)
{
  if(n < 2) {
    return n;
  }
  return fib(n-1) + fib(n-2);
}

int main()
{
  struct timespec start, finish;

  // Chew up some CPU time
  volatile int n=38, result;

  printf("Calculating fib(%d)...\n", n);

  clock_gettime(CLOCK_REALTIME, &start);
  result = fib(n);
  clock_gettime(CLOCK_REALTIME, &finish);

  struct timespec delta = timespec_diff(start, finish);

  printf("Finished in: %lu ms\n", (delta.tv_sec*1000) + (delta.tv_nsec/1000000));
  //printf("ms: %lf\n", delta_s*1000.0 + delta_ns/1000000.0);

  return 0;
}
