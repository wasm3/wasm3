/*
 * Build using:
 *   emcc dyn_callback.c -Os -s WASM=1 -s SIDE_MODULE=1 -o dyn_callback.wasm
 */

typedef int (*fptr_type)(int x, int y);

extern void pass_fptr(fptr_type fptr);

static int callback_add(int x, int y)
{
    return x+y;
}

static int callback_mul(int x, int y)
{
    return x*y;
}

void run_test()
{
    pass_fptr(callback_add);
    pass_fptr(callback_mul);
}
