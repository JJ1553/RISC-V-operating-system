#include "console.h"


//HOW TO USE THIS FILE:
/*
this needs a made companion.o file to work. you need to insert an elf into kernal memory
manually and then call elf load to run the elf file.

to do so, RUN:

bash mkcomp.sh ../user/bin/hello

where ../user/bin/hello could be any elf like trek or a home made file.
any non elf file will error, any badly formatted or wrong arch file will error.
badly formatted file examples (hex edited to be big endian instead of little) are 

called helloBADTest & trekBADTest.

THEN RUN:
make clean && make test.elf

make run-test-elf

done.
*/




#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "heap.h"
#include "elf.h"
#include "string.h"
#include "io.h"

// Declare the panic function prototype
void panic(const char *msg);

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
    //inputs: termio_raw
    //outputs: none
    //description: loads and executes an ELF file passed throught the termio_raw io_intf interface

    struct io_term ioterm;
    struct io_intf * termio;
    termio = ioterm_init(&ioterm, termio_raw); //initialize the terminal io_intf
    int tid;
    int result =0;

    struct io_lit elf_mem;
    void (*entrypoint)(struct io_intf *io); //function pointer to the entry point of the ELF file

    void *elf_buffer = _companion_f_start;
    size_t elf_size = _companion_f_end - _companion_f_start; //size of the ELF file

    struct io_intf *lit_intf = iolit_init(&elf_mem, elf_buffer, elf_size); //initialize the io_lit interface

    console_printf("\nLoading ELF file...\n");
    result = elf_load(lit_intf, &entrypoint); //load the ELF file using elf_load
    console_printf("loaded elf, Result: %d\n", result);

    if (result == 0) {
        ioprintf(termio, "Executing hello ELF file...\n");
        console_printf("Executing ELF file...\n\n");
        tid = thread_spawn("test_thread", (void *)entrypoint, termio_raw); //spawn a thread to execute the ELF file

        if (tid < 0)
            ioprintf(termio, "%s: Error %d\n", -result);
        else
            thread_join(tid); //join the thread
    } 
    else {
        ioprintf(termio, "Error: ELF loading failed\n");
        console_printf("Error: ELF loading failed with error (-6=EBADFMT): %d\n",result);
    }
}