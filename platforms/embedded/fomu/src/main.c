//
//  Wasm3 - high performance WebAssembly interpreter written in C.
//
//  Copyright © 2019 Steven Massey, Volodymyr Shymanskyy.
//  All rights reserved.
//

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <irq.h>
#include <usb.h>
#include <time.h>
#include <rgb.h>
#include <spi.h>
#include <generated/csr.h>

struct ff_spi *spi;

char *ltoa(long N, char *str, int base);

void uart_write(const char *str, unsigned len) {
    if (!usb_can_putc()) return;

    while (len) {
        while (!usb_can_putc())
            usb_poll();
        int bytes_written = usb_write(str, len);
        str += bytes_written;
        len -= bytes_written;
    }
}

void uart_print(const char *str) {
    uart_write(str, strlen(str));
}

#include "wasm3.h"

#include "extra/fib32.wasm.h"

#define FATAL(func, msg) {              \
  uart_print("Fatal: " func ": ");      \
  uart_print(msg); uart_print("\n");    \
  return false; }

bool run_wasm()
{
    M3Result result = m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    size_t fsize = fib32_wasm_len;

    uart_print("Loading WebAssembly...\n");

    IM3Environment env = m3_NewEnvironment ();
    if (!env) FATAL("m3_NewEnvironment", "failed");

    IM3Runtime runtime = m3_NewRuntime (env, 1024, NULL);
    if (!runtime) FATAL("m3_NewRuntime", "failed");

    IM3Module module;
    result = m3_ParseModule (env, &module, wasm, fsize);
    if (result) FATAL("m3_ParseModule", result);

    result = m3_LoadModule (runtime, module);
    if (result) FATAL("m3_LoadModule", result);

    IM3Function f;
    result = m3_FindFunction (&f, runtime, "fib");
    if (result) FATAL("m3_FindFunction", result);

    uart_print("Running...\n");

    result = m3_CallV (f, 24);
    if (result) FATAL("m3_Call", result);

    uint32_t value = 0;
    result = m3_GetResultsV (f, &value);
    if (result) FATAL("m3_GetResults", result);

    char buff[32];
    ltoa(value, buff, 10);

    uart_print("Result: ");
    uart_print(buff);
    uart_print("\n");

    return true;
}


__attribute__((section(".ramtext")))
void isr(void)
{
    unsigned int irqs;

    irqs = irq_pending() & irq_getmask();

    if (irqs & (1 << USB_INTERRUPT))
        usb_isr();
}

__attribute__((section(".ramtext")))
static void init(void)
{
    irq_setmask(0);
    irq_setie(1);
    usb_init();
    time_init();
    rgb_init();
}

void m3Yield ()
{
    usb_poll();
}

__attribute__((section(".ramtext")))
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    init();

    usb_connect();

    for (int i = 0; i< 10000; i++) {
        usb_poll();
        msleep(1);
    }

    uart_print("\nWasm3 v" M3_VERSION " on Fomu (" M3_ARCH "), build " __DATE__ " " __TIME__ "\n");

    rgb_set(0, 0, 255);
    if (run_wasm()) {
        rgb_set(0, 255, 0);
    } else {
        rgb_set(255, 0, 0);
    }

    while (1)
    {
        usb_poll();
    }
    return 0;
}
