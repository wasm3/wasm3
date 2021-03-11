#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "wasm_api.h"

/*
 * Helpers
 */

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
  fwrite(gString, 1, sizeof(gString)-1, stdout);
}

void test_write() {
  fwrite("Hello world\n", 1, 12, stdout);
}

void test_printf() {
  printf("Hello %s!\n", "printf");
}

void test_args(int argc, char **argv) {
  printf("Args: ");
  for (int i = 0; i < argc; i++) {
    printf("%s; ", argv[i]);
  }
  puts("");
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

void test_perf_fib(uint32_t n) {
  struct timespec start, finish;
  uint32_t result;

  printf("fib(%d) = ", n);
  fflush(stdout);

  // Chew up some CPU time
  clock_gettime(CLOCK_REALTIME, &start);
  result = fib(n);
  clock_gettime(CLOCK_REALTIME, &finish);

  struct timespec delta = timespec_diff(start, finish);

  //unsigned ms = (delta.tv_sec*1000) + (delta.tv_nsec/1000000);
  //printf("%d [%u ms]\n", result, ms);

  double fms = (delta.tv_sec*1000.0) + (delta.tv_nsec/1000000.0);
  printf("%d [%.3f ms]\n", result, fms);
}

void test_cat(char* fn) {
  int file = open(fn, O_RDONLY);
  if (file >= 0) {
    char c = 0;
    while (read(file, &c, sizeof(c)) > 0) {
      printf("%02x ", c);
    }
    close(file);
    puts("");
  } else {
    printf("Cannot open %s\n", fn);
  }
}

__attribute__((noinline))   void c() {          __builtin_trap();   }
__attribute__((noinline))   void b() {          c();   }
__attribute__((noinline))   void a() {          b();   }
__attribute__((noinline))   void test_trap() {  a();   }

/*
 * Main
 */

int main(int argc, char **argv)
{
  test_write();
  test_constructor();
  test_printf();
  test_args(argc, argv);
  test_gettime();
  test_random();
  if (0 == strcmp(argv[1], "trap")) {
    test_trap();
  }

  test_perf_fib(20);

  if (0 == strcmp(argv[1], "cat")) {
    test_cat(argv[2]);
  }

  puts("=== done ===");
  return 0;
}
