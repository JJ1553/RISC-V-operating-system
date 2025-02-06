#include "console.h"
#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "heap.h"
#include "elf.h"
#include "string.h"
#include "io.h"

// Define constants for memory limits
extern char _kimg_end[];
#define RAM_SIZE (8*1024*1024)
#define RAM_START 0x80000000UL
#define USER_START 0x80100000UL

// UART Definitions
#define UART0_IOBASE 0x10000000
#define UART1_IOBASE 0x10000100
#define UART0_IRQNO 10

// ELF memory region defined by companion.o section
extern char _companion_f_start[];
extern char _companion_f_end[];

void test_main(struct io_intf * termio_raw);

void main(void) {
    int i;
    void * mmio_base;
    struct io_intf * termio;

    console_init();
    intr_init();
    devmgr_init();
    thread_init();
    timer_init();
    heap_init(_kimg_end, (void*)USER_START);

    for (i = 0; i < 2; i++) 
    {
        mmio_base = (void*)UART0_IOBASE;
        mmio_base += (UART1_IOBASE - UART0_IOBASE) * i;
        uart_attach(mmio_base, UART0_IRQNO + i);
    }

    intr_enable();
    timer_start();

    int result1 = device_open(&termio, "ser", 1);

    if (result1 != 0)
        panic("Could not open ser1");

    test_main(termio);
}

void test_main(struct io_intf * termio_raw) {
    struct io_term ioterm;
    struct io_intf * termio;
    termio = ioterm_init(&ioterm, termio_raw);
    int tid;
    int result =0;

    struct io_lit elf_mem;
    void (*entrypoint)(struct io_intf *io);

    void *elf_buffer = _companion_f_start;
    size_t elf_size = _companion_f_end - _companion_f_start;

    struct io_intf *lit_intf = iolit_init(&elf_mem, elf_buffer, elf_size);

    result = elf_load(lit_intf, &entrypoint);
    if (result == 0) {
        ioprintf(termio, "Executing hello ELF file...\n");
        tid = thread_spawn("hello_thread", (void *)entrypoint, termio_raw);

        if (tid < 0)
            ioprintf(termio, "%s: Error %d\n", -result);
        else
            thread_join(tid);
    } 
    else {
        ioprintf(termio, "Error: ELF loading failed\n");
        console_printf("Error: ELF loading failed with error (-6=EBADFMT): %d\n",result);
    }
}