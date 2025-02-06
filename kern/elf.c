// elf.c - ELF executable loader

#include "elf.h"
#include "io.h"
#include "error.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "console.h"
#include "config.h"
#include "memory.h"


// ELF magic numbers and constants
#define ELF_MAGIC 0x464C457F // "x7FELF"
#define PT_LOAD 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EM_RISCV 243
#define EI_NIDENT 16
#define VADDR_START 0x80100000
#define VADDR_END 0x81000000
#define EI_VERSION 1
#define EI_OSABI 0
#define ET_EXEC 2

#define PF_X 0x1 // Executable
#define PF_W 0x2 // Writable
#define PF_R 0x4 // Readable

// ELF header structure
struct elf_header {
    unsigned char e_ident[EI_NIDENT]; // Identification bytes
        //EI_MAG0-3: 0x7F, 'E', 'L', 'F'

        //EI_CLASS 4: ELFCLASSNONE (0) = invalid, 
                    //ELFCLASS32 (1) = 32-bit, 
                    //ELFCLASS64 (2) = 64-bit (we use this)

        //EI_DATA 5: ELFDATANONE (0) = unknown, 
                    //ELFDATA2LSB (1) = little endian (we use this), 
                    //ELFDATA2MSB (2) = big endian 

        //EI_VERSION 6: EV_NONE (0) = invalid, 
                        //EV_CURRENT (1) = current 
                        //version number of elf specification

        //EI_OSABI 7: ELFOSABI_NONE (0) = UNIX System V = ELFOSABI_SYSV (we use this), 
                    //ELFOSABI_LINUX (3) = Linux

        //EI_ABIVERSION 8: should be 0 for ELFOSABI_SYSV and our project (1 if you have custom ABI)

        //EI_PAD 9-15: padding... should be 0
        

    uint16_t e_type;                  // ET_NONE (0) = invalid, 
                                        //ET_REL (1) = relocatable, 
                                        //ET_EXEC (2) = executable, 
                                        //ET_DYN (3) = shared object, 
                                        //ET_CORE (4) = core file

    uint16_t e_machine;               // Machine type: RISC-V = 243
    uint32_t e_version;               // Object file version: EV_NONE = 0, EV_CURRENT = 1 (we use this)
    uint64_t e_entry;                 // Entry point address of program
    uint64_t e_phoff;                 // Program header offset
    uint64_t e_shoff;                 // Section header offset
    uint32_t e_flags;                 // Processor-specific flags
    uint16_t e_ehsize;                // ELF header size
    uint16_t e_phentsize;             // Program header entry size
    uint16_t e_phnum;                 // Number of program header entries
    uint16_t e_shentsize;             // Section header entry size
    uint16_t e_shnum;                 // Number of section header entries
    uint16_t e_shstrndx;              // Section header string table index
};

// Program header structure
struct prog_header {
    uint32_t p_type;                  // Segment type: 
    uint32_t p_flags;                 // Segment flags
    uint64_t p_offset;                // Segment file offset
    uint64_t p_vaddr;                 // Segment virtual address
    uint64_t p_paddr;                 // Segment physical address
    uint64_t p_filesz;                // Segment size in file
    uint64_t p_memsz;                 // Segment size in memory
    uint64_t p_align;                 // Segment alignment
};

//inptus: io interface from which to load the elf, pointer to void (*entry)(struct io_intf *io), which is a function pointer elf_load fills in with the address of the entry point
//output: 0 on success or a negative error code on error
//Loads an executable ELF file into memory and returns the entry point. Checks the ELF header and program headers, then loads the program segments into memory according to ELF documentation
int elf_load(struct io_intf *io, void (**entryptr)(void)) {
    struct elf_header ehdr;
    struct prog_header phdr;
    long result;

    // Read ELF header for the size of the header
    result = ioread(io, &ehdr, sizeof(ehdr));
    
    if (result < 0) {
        return result;
    }
    if (result != sizeof(ehdr)) 
        return -EIO;


       // Print ELF header values
    console_printf("\nELF Header:\n");
    console_printf("  Magic: %02x %02x %02x %02x\n", ehdr.e_ident[0], ehdr.e_ident[1], ehdr.e_ident[2], ehdr.e_ident[3]);
    console_printf("  e_ident[4] == ELFCLASS64: %s \n", ehdr.e_ident[4] == ELFCLASS64 ? "true" : "false");
    console_printf("  e_ident[5] == ELFDATA2LSB: %s \n", ehdr.e_ident[5] == ELFDATA2LSB ? "true" : "false");
    console_printf("  e_ident[6] == EI_VERSION: %s \n", ehdr.e_ident[6] == EI_VERSION ? "true" : "false");
    console_printf("  e_ident[7] == EI_OSABI: %s \n", ehdr.e_ident[7] == EI_OSABI ? "true" : "false");
    console_printf("  e_machine == EM_RISCV: %s \n", ehdr.e_machine == EM_RISCV ? "true" : "false");
    console_printf("  e_type == ET_EXEC: %s \n", ehdr.e_type == ET_EXEC ? "true" : "false");


    // Validate ELF header, magic number "0x7f, 'E', 'L', 'F' " from official ELF documentation and therefore not "#define" ed above
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' || ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') 
        return -EBADFMT;
    if (ehdr.e_ident[4] != ELFCLASS64) //ensure 64-bit
        return -EBADFMT;
    if (ehdr.e_ident[5] != ELFDATA2LSB) //ensure little endian
        return -EBADFMT;
    if(ehdr.e_ident[6] != EI_VERSION) //EV_CURRENT
        return -EBADFMT;
    if(ehdr.e_ident[7] != EI_OSABI) // ensure UNIX System V
        return -EBADFMT;
    if (ehdr.e_machine != EM_RISCV) //ensure RISC-V
        return -EBADFMT;
    if(ehdr.e_type != ET_EXEC) //ensure executable
        return -EBADFMT;


    // Process program headers
    for (int i = 0; i < ehdr.e_phnum; i++) {

        // Seek to program header
        result = ioseek(io, (ehdr.e_phoff + i * ehdr.e_phentsize));
        if (result < 0) 
            return result;

        // Read program header
        result = ioread(io, &phdr, sizeof(phdr));
        if (result < 0) {
            console_printf("second");
            return result;
        }
        if (result != sizeof(phdr)) 
            return -EIO;

        // Loadable segment
        if (phdr.p_type == PT_LOAD) {
            // Validate segment address
            if (phdr.p_vaddr < USER_START_VMA || phdr.p_vaddr + phdr.p_memsz > USER_END_VMA) { 
                return -EBADFMT;
            }

            // Seek to the segment offset in the file, ie point to the start of the segment
            result = ioseek(io, phdr.p_offset);
            if (result < 0) 
                return result;

            // Read the segment data into memory, so with io at the start, read filesz bytes into memory starting at vaddr
            memory_alloc_and_map_range(phdr.p_vaddr, phdr.p_memsz, PTE_R | PTE_W | PTE_U);
            result = ioread(io, (void*)phdr.p_vaddr, phdr.p_filesz);
            if (result < 0) {
                return result;
            }
            if (result != phdr.p_filesz) 
                return -EIO;

            // Zero out the remaining memory if memsz > filesz
            if (phdr.p_memsz > phdr.p_filesz) {
                memset((void*)(phdr.p_vaddr + phdr.p_filesz), 0, (size_t)(phdr.p_memsz - phdr.p_filesz));
            }
            else if (phdr.p_memsz < phdr.p_filesz) {
                return -EBADFMT;
            }
            int read = (phdr.p_flags & PF_R) ? PTE_R : 0; 
            int write = (phdr.p_flags & PF_W) ? PTE_W : 0; 
            int execute = (phdr.p_flags & PF_X) ? PTE_X : 0; 
            memory_set_range_flags((void*)phdr.p_vaddr, phdr.p_memsz, read | write | execute | PTE_U);

        }
    }

    // Print Program Header values
    console_printf("\nProgram Header:\n");
    console_printf("  p_type: %u\n", phdr.p_type);
    console_printf("  p_flags: %u\n", phdr.p_flags);
    console_printf("  p_offset: 0x%lx\n", phdr.p_offset);
    console_printf("  p_vaddr: 0x%lx\n", phdr.p_vaddr);
    console_printf("  p_paddr: 0x%lx\n", phdr.p_paddr);
    console_printf("  p_filesz: %lu\n", phdr.p_filesz);
    console_printf("  p_memsz: %lu\n", phdr.p_memsz);
    console_printf("  p_align: %lu\n\n", phdr.p_align);

    // Set entry point
    *entryptr = (void (*)(void))ehdr.e_entry;
    console_printf("Entry point: 0x%lx\n\n", *entryptr);

    return 0;
}