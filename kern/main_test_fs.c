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
    struct io_lit fs_lit;
    struct io_intf* fs_io;
    int result;
    char* name;
    // This file uses a companion file loaded into kernel memory that only has my test file. 
    // I do that so I can show my iolit functions work.


    // Initialize the filesystem interface
    fs_io = iolit_init(&fs_lit, _companion_f_start, _companion_f_end - _companion_f_start);
    
    // TEST 1: Mount the filesystem
    console_printf("Test 1: Mounting File System\n");
    fs_mount(fs_io);
    

    // TEST 2: Open file with name "hello"
    console_printf("Test 2: Open File\n");
    struct io_intf* test_io_ptr = NULL;
    // Open the file named "hello"
    name = "test";
    result = fs_open(name, &test_io_ptr); // Pass pointer to test_io_ptr
    if (result != 0){   
        panic("fs_open failed");
        return -1;
    }
    file_t* my_file = (file_t*) test_io_ptr;        //
    console_printf("File Inode: %d\n", my_file->inode);
    console_printf("File Pos: %d\n", my_file->file_pos);
    console_printf("File Length: %d\n\n", my_file->byte_len);

    // TEST 3: Gets Length of File and Block Size
    console_printf("Test 3: Gets File Length and Block Size\n");
    uint64_t file_length;
    uint64_t block_size;
    ioctl(test_io_ptr, IOCTL_GETLEN, &file_length);
    ioctl(test_io_ptr, IOCTL_GETBLKSZ, &block_size);
    console_printf("File Length: %d\n", file_length);
    console_printf("Block Size: %d\n\n", block_size);

 
    // TEST 4: Read to file
    int n = 168;
    console_printf("Test 4: Read %d bytes from file\n", n);
    char readBuffer[n];
    result = fs_read(test_io_ptr, readBuffer, sizeof(readBuffer)); // sizeof(buffer) ensures correct size
    if (result < 0){   
        panic("fs_read failed");
        return -1;
    }
    // Print out each byte read from the file
    for(int i = 0; i < result; i++) {
        console_putchar(readBuffer[i]);
    }
    console_printf("\nBytes Read: %d\n\n", result);
    
    // TEST 5: Set and Get file position
    int x = 0;
    console_printf("Test 5: Set file position to %d and get file position\n",x);
    int file_pos;
    ioseek(test_io_ptr, x);
    console_printf("Set File Pos to %d\n", x);
    ioctl(test_io_ptr, IOCTL_GETPOS, &file_pos);
    console_printf("New File Pos: %d\n\n", my_file->file_pos);

    // TEST 6: Write to a file
    char writeBuffer[32] = "I think my ECE 391 test worked!!";
    console_printf("Test 6: Write %d bytes to file\n",sizeof(writeBuffer));
    int result1 = fs_write(test_io_ptr, writeBuffer, sizeof(writeBuffer)); // sizeof(buffer) ensures correct size
    if (result < 0){   
        panic("fs_write failed");
        return -1;
    }
    // Print out each byte read from the file
    x = 0;
    ioseek(test_io_ptr, x);
    char readBuffer2[168];
    result = fs_read(test_io_ptr, readBuffer2, sizeof(readBuffer2)); // sizeof(buffer) ensures correct size
    if (result < 0){   
        panic("fs_read failed");
        return -1;
    }
    for(int i = 0; i < result; i++) {
        console_putchar(readBuffer2[i]);
    }
    console_printf("\nBytes Written: %d\n\n", result1);

    // TEST 7: Reading beyond files length
    x = 86;
    n = 100;
    ioseek(test_io_ptr, x);
    console_printf("Test 7: Read beyond file length\n", n);
    char readBuffer3[n];
    result = fs_read(test_io_ptr, readBuffer3, sizeof(readBuffer3)); // sizeof(buffer) ensures correct size
    if (result < 0){   
        panic("fs_read failed");
        return -1;
    }
     // Print out each byte read from the file
    for(int i = 0; i < result; i++) {
        console_putchar(readBuffer3[i]);
    }
    console_printf("\nBytes Read: %d\n\n", result);

    // TEST 8: Writing beyond files length
    char writeBuffer2[32] = "Did my test work? I am not sure.";
    x = 150;
    ioseek(test_io_ptr, x);
    console_printf("Test 8: Write %d bytes to file\n",sizeof(writeBuffer2));
    result1 = fs_write(test_io_ptr, writeBuffer2, sizeof(writeBuffer2)); // sizeof(buffer) ensures correct size
    if (result < 0){   
        panic("fs_write failed");
        return -1;
    }
    // Print out each byte read from the file
    x = 0;
    ioseek(test_io_ptr, x);
    char readBuffer4[168];
    result = fs_read(test_io_ptr, readBuffer4, sizeof(readBuffer4)); // sizeof(buffer) ensures correct size
    if (result < 0){   
        panic("fs_read failed");
        return -1;
    }
    for(int i = 0; i < result; i++) {
        console_putchar(readBuffer4[i]);
    }
    console_printf("\nBytes Written: %d\n\n", result1);

    // TEST 9: 
    console_printf("Test 9: IOLIT CTL \n");
    uint64_t iolit_file_length;
    ioctl(fs_io, IOCTL_GETLEN, &iolit_file_length);
    console_printf("IOLIT_IOCTL: File Length: %d\n", iolit_file_length);

    x = 10;
    int iolit_file_pos;
    ioctl(fs_io,IOCTL_SETPOS, &x);
    console_printf("IOLIT_IOCTL: Set File Pos to %d\n", x);
    ioctl(fs_io,IOCTL_GETPOS, &iolit_file_pos);
    console_printf("IOLIT_IOCTL: New File Pos: %d\n\n", iolit_file_pos);

    // TEST 10: Close file
    console_printf("Test 10: Close file\n");
    fs_close(test_io_ptr);
    return 0;

}