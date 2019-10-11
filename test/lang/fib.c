/*
 * emcc -g0 -O3 -s SIDE_MODULE=1 -s STRICT=1 -s WASM=1 -s ONLY_MY_CODE=1 -o fib.c.wasm fib.c
 * gcc -g0 -O3 fib.c -o fib.c.elf
 */

#include <stdint.h>
#define WASM_EXPORT __attribute__((used)) __attribute__((visibility ("default")))

WASM_EXPORT
uint32_t fib(uint32_t n)
{
    if(n < 2) {
        return n;
    }
    return fib(n-1) + fib(n-2);
}

int parseInt(char* str) {
    int res = 0;
    for (int i = 0; str[i] != '\0'; ++i) {
        res = res * 10 + str[i] - '0';
    }
    return res;
}

WASM_EXPORT
int main(int args, char* argv[]) {
    uint32_t n = parseInt(argv[1]);
    return fib(n);
}
