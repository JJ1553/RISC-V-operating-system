#include "console.h"
#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "heap.h"
#include "virtio.h"
#include "halt.h"
#include "elf.h"
#include "fs.h"
#include "string.h"
//#include "vioblk.c"

//           end of kernel image (defined in kernel.ld)
extern char _kimg_end[];

#define RAM_SIZE (8*1024*1024)
#define RAM_START 0x80000000UL
#define KERN_START RAM_START
#define USER_START 0x80100000UL

#define UART0_IOBASE 0x10000000
#define UART1_IOBASE 0x10000100
#define UART0_IRQNO 10

#define VIRT0_IOBASE 0x10001000
#define VIRT1_IOBASE 0x10002000
#define VIRT0_IRQNO 1

extern char _companion_f_start[];
extern char _companion_f_end[];

int main(void) {
    void * mmio_base;
    int i;

    console_init();
    intr_init();
    devmgr_init();
    thread_init();
    timer_init();

    heap_init(_kimg_end, (void*)USER_START);

    for (i = 0; i < 2; i++) {
        mmio_base = (void*)UART0_IOBASE;
        mmio_base += (UART1_IOBASE-UART0_IOBASE)*i;
        uart_attach(mmio_base, UART0_IRQNO+i);
    }

    for (i = 0; i < 8; i++) {
        mmio_base = (void*)VIRT0_IOBASE;
        mmio_base += (VIRT1_IOBASE-VIRT0_IOBASE)*i;
        virtio_attach(mmio_base, VIRT0_IRQNO+i);
    }

    console_printf("Device attached to registers!\n");
    intr_enable();

    struct io_intf * io = NULL;
    console_printf("Opening Device:\n");
    device_open(&io, "blk", 0);
    console_printf("Opened Device!\n");

    //          Check Ioctl commands
    uint64_t data = 0;
    const uint64_t * dataptr = &data;
    console_printf("\nChecking ioctl commands:\n");
    ioctl(io, IOCTL_GETLEN, (void *)dataptr);
    console_printf("Check get length: length = %u\n", *dataptr);
    ioctl(io, IOCTL_GETBLKSZ, (void *)dataptr);
    console_printf("Check get blksz: blksz = %u\n", *dataptr);
    ioctl(io, IOCTL_GETPOS, (void *)dataptr);
    console_printf("Check get position: position = %u\n", *dataptr);
    data = 4;
    ioctl(io, IOCTL_SETPOS, (void *)dataptr);
    ioctl(io, IOCTL_GETPOS, (void *)dataptr);
    console_printf("Check set position, set to 4: position = %u\n", *dataptr);

    


    //          Test 1: reading and writing a partial block
    console_printf("\nFirst Test: Reading and writing a partial block\n");
    //          Initialize our buffers
    uint8_t w_buffer[4];
    uint8_t r_buffer[4];
    unsigned long bufsz = 4;
    long result;
    
    //          Write to one partial
    for(i=0; i<4; i++)
        w_buffer[i] = i;
    result = iowrite(io, (const void*) w_buffer, bufsz);
    if(result != bufsz){
        panic("Full block write failed");
        return -1;
    }
    console_printf("Successfully wrote! Check write size: %u\n", result);

    //          Read what we just wrote to the disc
    result = ioread(io, r_buffer, bufsz);
    if(result != bufsz){
        panic("full block read failed");
        return -1;
    }
    console_printf("Successfully read! Check bytes read: %u\n", result);
    for(int i=0; i<bufsz; i++){
        if(w_buffer[i] != r_buffer[i])
            panic("read and write buffers not equal");
    }
    console_printf("Read and write buffers are equal! Success!\n");

    data = 0;
    ioctl(io, IOCTL_SETPOS, (void *)dataptr);

    //          Test 2: reading and writing a full block
    console_printf("\nSecond Test: Reading and writing a full block\n");
    //          Initialize our buffers
    uint8_t write_buffer[512];
    uint8_t read_buffer[512];
    bufsz = 512;
    
    //          Write to one full block
    for(i=0; i<512; i++)
        write_buffer[i] = i % 256;
    result = iowrite(io, (const void*) write_buffer, bufsz);
    if(result != bufsz){
        panic("Full block write failed");
        return -1;
    }
    console_printf("Successfully wrote! Check write size: %u\n", result);
    data = 0;
    ioctl(io, IOCTL_SETPOS, (void *)dataptr);
    
    //          Read what we just wrote to the disc
    result = ioread(io, read_buffer, bufsz);
    if(result != bufsz){
        panic("full block read failed");
        return -1;
    }
    console_printf("Successfully read! Check bytes read: %u\n", result);
    for(int i=0; i<bufsz; i++){
        if(write_buffer[i] != read_buffer[i])
            panic("read and write buffers not equal");
    }
    console_printf("Read and write buffers are equal! Success!\n");


    //          Test 3: reading and writing more than blksz
    console_printf("\nThird Test: Reading and writing into more than 1 block\n");
    //          Initialize our buffers
    uint8_t wri_buffer[530];
    uint8_t rd_buffer[530];
    bufsz = 530;
    
    //          Write to more than blksz
    for(i=0; i<530; i++)
        wri_buffer[i] = i % 256;
    result = iowrite(io, (const void*) wri_buffer, bufsz);
    if(result != bufsz){
        panic("Full block write failed");
        return -1;
    }
    console_printf("Successfully wrote! Check write size: %u\n", result);

    //          Read what we just wrote to the disc
    result = ioread(io, rd_buffer, bufsz);
    if(result != bufsz){
        panic("full block read failed");
        return -1;
    }
    console_printf("Successfully read! Check bytes read: %u\n", result);
    for(int i=0; i<bufsz; i++){
        if(wri_buffer[i] != rd_buffer[i])
            panic("read and write buffers not equal");
    }
    console_printf("Read and write buffers are equal! Success!\n");


}