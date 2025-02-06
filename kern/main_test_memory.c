#ifdef MAIN_TRACE
#define TRACE
#endif

#ifdef MAIN_DEBUG
#define DEBUG
#endif

#define INIT_PROC "init0" // name of init process executable

#include "console.h"
#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "memory.h"
#include "heap.h"
#include "virtio.h"
#include "halt.h"
#include "elf.h"
#include "fs.h"
#include "string.h"
#include "process.h"
#include "config.h"
#include "io.h"

extern char _companion_f_start[];
extern char _companion_f_end[];

void main(void) {
    console_init();
    memory_init();
    devmgr_init();
    thread_init();

    /*
    console_printf("\nTest 1: Mapping User Pages\n");
    //Allocate 3 pages, then change the flags of two of them
    memory_alloc_and_map_range (0xC0000000, PAGE_SIZE*3, PTE_R | PTE_W | PTE_X | PTE_U);
    memory_set_range_flags((void*) 0xC0000000, PAGE_SIZE*2, PTE_U);
    memory_unmap_and_free_user();
    */
    
    console_printf("\nTest 2: validate vptr length\n");
    //allocate 5 pages and check that a pointer of length 5 pages is valid 
    //(or change to 6 for invalid)
    memory_alloc_and_map_range(0xC1000000, PAGE_SIZE*5, PTE_R | PTE_W | PTE_X | PTE_U);
    //Change the length to 4096*6 to show an invalid vptr length
    int check = memory_validate_vptr_len((void *)0xc1000000, PAGE_SIZE*5, PTE_R | PTE_W | PTE_X | PTE_U);
    if(check == 0)
        console_printf("vptr length valid!\n");
    else
        console_printf("vptr length invalid :(\n");
    memory_unmap_and_free_user();

    
    console_printf("\nTest 3: validate vstr\n");
    //Allocate a page of all 'A' with null character at the end to show valid string 
    //(remove null character to show invalid string))
    char * string = memory_alloc_and_map_page(0xc0000000, PTE_W | PTE_R | PTE_U);
    for(int i=0; i<PAGE_SIZE; i++){
        string[i] = 'A';
    }
    string[PAGE_SIZE-1] = '\0';     //Comment out this line to show an invalid string
    int result = memory_validate_vstr(string, PTE_W | PTE_R | PTE_U);
    if(result == 0)
        console_printf("String valid!\n");
    else
        console_printf("string invalid :(\n");
    memory_unmap_and_free_user();


    /*
    console_printf("\nTest 4: Reading without read permissions\n");
    //Allocate a page, then write and read a 4 from mem. Remove read permissions and show error
    int * number = memory_alloc_and_map_page(0xc0000000,  PTE_R | PTE_W | PTE_U);
    number[0] = 4;
    int num_read = number[0];
    console_printf("Read character with permissions, result: %d\n", num_read);
    number[0] = 5;
    memory_set_page_flags((void *)0xC0000000, PTE_U);
    console_printf("Changed number to 5 and disabled read permissions, attempting read:\n");
    num_read = number[0];
    memory_unmap_and_free_user();
    */

    /*
    console_printf("\nTest 5: Writing without write permissions\n");
    //Allocate a page and write to mem with permissions, then disable permissions and try again
    int * number = memory_alloc_and_map_page(0xC0000000,  PTE_R | PTE_W | PTE_U);
    number[0] = 4;
    console_printf("Wrote character with permissions, result: %d\n", number[0]);
    memory_set_page_flags((void *)0xC0000000, PTE_R | PTE_U);
    console_printf("Disabled write permissions, attempting write:\n");
    number[0] = 5;
    memory_unmap_and_free_user();
    */

   /*
    console_printf("\nTest 6: Executing without execute permissions\n");
    //Attempt to use goto on a memory address that does not have executable permissions
    unsigned * exec = memory_alloc_and_map_page(0xC0000000, PTE_R | PTE_W);
    goto *exec;
    //console_printf("Executed instruction, result: %d\n", num);
    */


    console_printf("\n---------------End of Tests---------------\n");










    /*
    PREVIOUS TEST CODE -from unknown commented by Josh
    struct io_term ioterm;
    struct io_intf * termio;
    struct io_intf * termio_raw;

    int result = 0;
    struct io_lit elf_mem;
    void (*entrypoint)(void);

    void *elf_buffer = _companion_f_start;
    size_t elf_size = _companion_f_end - _companion_f_start; //size of the ELF file

    struct io_intf *lit_intf = iolit_init(&elf_mem, elf_buffer, elf_size); //initialize the io_lit interface


    termio = ioterm_init(&ioterm, termio_raw); //initialize the terminal io_intf
    result = elf_load(lit_intf, &entrypoint); //load the ELF file using elf_load
    console_printf("Entry Point: %x\n", entrypoint);
    */
}
