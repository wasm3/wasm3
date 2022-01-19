#include <stddef.h>
#include <stdint.h>

extern int sum(int, int);
extern int ext_memcpy(void*, const void*, size_t);

int32_t counter = 0;

#define WASM_EXPORT __attribute__((used)) __attribute__((visibility ("default")))

int WASM_EXPORT test(int32_t arg1, int32_t arg2)
{
    int x = arg1 + arg2;
    int y = arg1 - arg2;
    return sum(x, y) / 2;
}

int64_t WASM_EXPORT test_memcpy(void)
{
    int64_t x = 0;
    int32_t low = 0x01234567;
    int32_t high = 0x89abcdef;
    ext_memcpy(&x, &low, 4);
    ext_memcpy(((uint8_t*)&x) + 4, &high, 4);
    return x;
}

int32_t WASM_EXPORT test_counter_get()
{
    return counter;
}

void WASM_EXPORT test_counter_inc()
{
    ++counter;
}

void WASM_EXPORT test_counter_add(int32_t inc_value)
{
    counter += inc_value;
}
