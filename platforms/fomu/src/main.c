#include <stdio.h>
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

int puts(const char *str) {
    uart_write(str, strlen(str));
}

#define FATAL(msg, ...) { puts("Fatal: " msg "\n"); return false; }

#include "m3/m3.h"
#include "m3/m3_env.h"
#include "m3/extra/fib32.wasm.h"

bool run_wasm()
{
    M3Result result = c_m3Err_none;

    uint8_t* wasm = (uint8_t*)fib32_wasm;
    uint32_t fsize = fib32_wasm_len-1;

    puts("Loading WebAssembly...\n");

    IM3Module module;
    result = m3_ParseModule (& module, wasm, fsize);
    if (result) FATAL("m3_ParseModule: %s", result);

    IM3Runtime env = m3_NewRuntime (1024);
    if (!env) FATAL("m3_NewRuntime");

    result = m3_LoadModule (env, module);
    if (result) FATAL("m3_LoadModule: %s", result);

    IM3Function f;
    result = m3_FindFunction (&f, env, "fib");
    if (result) FATAL("m3_FindFunction: %s", result);

    puts("Running...\n");

    const char* i_argv[2] = { "24", NULL };
    result = m3_CallWithArgs (f, 1, i_argv);

    if (result) FATAL("m3_CallWithArgs: %s", result);

    long value = * (uint64_t *) (env->stack);
    char buff[32];
    ltoa(value, buff, 10);

    puts("Result: ");
    puts(buff);
    puts("\n");

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

    puts("\nwasm3 on Fomu, build " __DATE__ " " __TIME__ "\n");

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
