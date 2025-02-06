// memory.c - Memory management
//

#ifndef TRACE
#ifdef MEMORY_TRACE
#define TRACE
#endif
#endif

#ifndef DEBUG
#ifdef MEMORY_DEBUG
#define DEBUG
#endif
#endif

#include "config.h"

#include "memory.h"
#include "console.h"
#include "halt.h"
#include "heap.h"
#include "csr.h"
#include "string.h"
#include "error.h"
#include "thread.h"
#include "process.h"

#include <stdint.h>

// EXPORTED VARIABLE DEFINITIONS
//

char memory_initialized = 0;
uintptr_t main_mtag;

// IMPORTED VARIABLE DECLARATIONS
//

// The following are provided by the linker (kernel.ld)

extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// INTERNAL TYPE DEFINITIONS
//

union linked_page {
    union linked_page * next;
    char padding[PAGE_SIZE];
};

struct pte {
    uint64_t flags:8;
    uint64_t rsw:2;
    uint64_t ppn:44;
    uint64_t reserved:7;
    uint64_t pbmt:2;
    uint64_t n:1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN2(vma) (((vma) >> (9+9+12)) & 0x1FF)
#define VPN1(vma) (((vma) >> (9+12)) & 0x1FF)
#define VPN0(vma) (((vma) >> 12) & 0x1FF)
#define MIN(a,b) (((a)<(b))?(a):(b))
#define POFFSET(vma) ((vma) & 0xFFF)

// INTERNAL FUNCTION DECLARATIONS
//

static inline int wellformed_vma(uintptr_t vma);
static inline int wellformed_vptr(const void * vp);
static inline int aligned_addr(uintptr_t vma, size_t blksz);
static inline int aligned_ptr(const void * p, size_t blksz);
static inline int aligned_size(size_t size, size_t blksz);

static inline uintptr_t active_space_mtag(void);
static inline struct pte * mtag_to_root(uintptr_t mtag);
static inline struct pte * active_space_root(void);

static inline void * pagenum_to_pageptr(uintptr_t n);
static inline uintptr_t pageptr_to_pagenum(const void * p);

static inline void * round_up_ptr(void * p, size_t blksz);
static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz);
static inline size_t round_up_size(size_t n, size_t blksz);
static inline void * round_down_ptr(void * p, size_t blksz);
static inline size_t round_down_size(size_t n, size_t blksz);
static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz);

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

static inline void sfence_vma(void);

uintptr_t memory_space_switch(uintptr_t mtag) {
    uintptr_t old_mtag = csrrw_satp(mtag);
    sfence_vma();
    return old_mtag;
}

// INTERNAL GLOBAL VARIABLES
//

static union linked_page * free_list;

static struct pte main_pt2[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));

// EXPORTED VARIABLE DEFINITIONS
//

// EXPORTED FUNCTION DEFINITIONS
// 

    // Input: None
    // Output: None
    // Purpose: Initializes the memory manager. Must be called before calling any other functions of the memory manager.
void memory_init(void) {

    const void * const text_start = _kimg_text_start;
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;
    union linked_page * page;
    void * heap_start;
    void * heap_end;
    size_t page_cnt;
    uintptr_t pma;
    const void * pp;

    trace("%s()", __func__);

    assert (RAM_START == _kimg_start);

    kprintf("           RAM: [%p,%p): %zu MB\n",
        RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    kprintf("  Kernel image: [%p,%p)\n", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)
    
    if (MEGA_SIZE < _kimg_end - _kimg_start)
        panic("Kernel too large");

    // Initialize main page table with the following direct mapping:
    // 
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB
    
    // Identity mapping of two gigabytes (as two gigapage mappings)
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void*)pma, PTE_R | PTE_W | PTE_G);
    
    // Third gigarange has a second-level page table
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] =
        ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Enable paging. This part always makes me nervous.

    main_mtag =  // Sv39
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
        pageptr_to_pagenum(main_pt2);
    
    csrw_satp(main_mtag); // set satp to main_mtag
    sfence_vma(); // Flush TLB

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end; // start the heap at the end of kernel image
    heap_end = round_up_ptr(heap_start, PAGE_SIZE); // round up to the next page boundary
    if (heap_end - heap_start < HEAP_INIT_MIN) { // if the heap is less than the minimum size
        heap_end += round_up_size ( // round up to the next page boundary
            HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE); 
    }

    if (RAM_END < heap_end)
        panic("Not enough memory");
    
    // Initialize heap memory manager

    heap_init(heap_start, heap_end); // initialized heap

    kprintf("Heap allocator: [%p,%p): %zu KB free\n",
        heap_start, heap_end, (heap_end - heap_start) / 1024);

    free_list = heap_end; // heap_end is page aligned
    page_cnt = (RAM_END - heap_end) / PAGE_SIZE; // RAM_END is page aligned

    kprintf("Page allocator: [%p,%p): %lu pages free\n",
        free_list, RAM_END, page_cnt); // free_list is a linked list of pages

    // Put free pages on the free page list
    // memory_alloc_page and memory_free_page).

    for(void* pp = heap_end; pp < RAM_END; pp += PAGE_SIZE){ // free_list is a linked list of pages
        page = pp; // pp is a pointer to the page
        page->next = free_list; // next points to the next page in the list
        free_list = page; // free_list points to the current page
    }
    
    // Allow supervisor to access user memory. We could be more precise by only
    // enabling it when we are accessing user memory, and disable it at other
    // times to catch bugs.

    csrs_sstatus(RISCV_SSTATUS_SUM); // Supervisor User Memory access

    memory_initialized = 1;
}


void memory_space_reclaim(void) {
    // Input: None
    // Output: None
    // Purpose: Switches the active memory space to the main memory space and reclaims the memory space that was active on entry. 
    // All physical pages mapped by a user mapping are reclaimed.   
    memory_unmap_and_free_user(); // unmaps and frees all pages with the U bit set in the PTE flags
    csrw_satp(main_mtag); // set satp to main_mtag
    sfence_vma(); // Flush TLB
    // uintptr_t vma;
}

void * memory_alloc_page(void){
    // Input: None
    // Output: void*
    // Purpose: Allocates a physical page of memory. Returns a pointer to the direct-mapped address of the page.
    void * pp;

    if(free_list == NULL){
        panic("The free list is empty, unable to alloc_page!");
    }
    //1. Remove from free page list
    //2. return it
    pp = free_list; // pp points to the first page in the free list
    free_list = free_list->next; // free_list points to the next page in the list
    memset(pp, 0, PAGE_SIZE); // set the page to 0
    sfence_vma(); // Flush TLB
    return pp; // return the page
}

void memory_free_page (void* pp){
    // Input: void*
    // Output: None
    // Purpose: Returns a physical memory page to the physical page allocator. The page must have been previously allocated by memory_alloc_page.
    ((union linked_page*)pp)->next = free_list; // next points to the next page in the list
    free_list = pp; // free_list points to the current page
    sfence_vma(); // Flush TLB
}

void * memory_alloc_and_map_page (uintptr_t vma, uint_fast8_t rwxug_flags){
    // Input: uintptr_t, uint_fast8_t
    // Output: void*
    // Purpose: Allocates and maps a physical page.
    void * pp = memory_alloc_page(); // allocates a physical page
    struct pte* my_pte = walk_pt(active_space_root(), vma, 1); // walks the page table hierarchy to find the PTE for the specified virtual address
    if(my_pte == NULL){ // if the PTE is NULL
        panic("walk_pt failed in memory_alloc_and_map_page"); // panic
    }
    *my_pte = leaf_pte(pp, rwxug_flags); // set the PTE to the leaf PTE
    sfence_vma(); // Flush TLB
    return (void*) vma;
}

void * memory_alloc_and_map_range (uintptr_t vma, size_t size, uint_fast8_t rwxug_flags){
    // Input: uintptr_t, size_t, uint_fast8_t
    // Output: void*
    // Purpose: Allocates and maps multiple physical pages in an address range. Equivalent to calling memory_alloc_and_map_page for every page in the range.
    for(uintptr_t pp = vma; pp < vma + size; pp += PAGE_SIZE){ // for each page in the range
        memory_alloc_and_map_page(pp, rwxug_flags); // allocates and maps a physical page
    }
    sfence_vma(); // Flush TLB
    return (void*) vma;
}

void memory_set_page_flags(const void *vp, uint8_t rwxug_flags){
    // Input: const void*, uint8_t
    // Output: None
    // Purpose: Sets the flags of a page to the specified flags.
    struct pte * my_pte = walk_pt(active_space_root(), (uintptr_t)vp, 0); // walks the page table hierarchy to find the PTE for the specified virtual address
    my_pte->flags = rwxug_flags | PTE_A | PTE_D | PTE_V; // set the flags of the PTE to the specified flags
    sfence_vma(); //Flush TLB
}

void memory_set_range_flags(const void *vp, size_t size, uint_fast8_t rwxug_flags){
    // Input: const void*, size_t, uint_fast8_t
    // Output: None
    // Purpose: Changes the PTE flags for all pages in a mapped range.
    const void *pp;

    for(pp = vp; pp-vp < size; pp+=PAGE_SIZE){ // for each page in the range
        memory_set_page_flags(pp, rwxug_flags); // set the flags of the page to the specified flags
    }
    sfence_vma(); //Flush TLB
}

void memory_unmap_and_free_user(void){
    // Input: None
    // Output: None
    // Purpose: Unmaps and frees all pages with the U bit set in the PTE flags.

    //void * pp = round_up_ptr(_kimg_end, PAGE_SIZE);  //heap_end
    uintptr_t vma;
    struct pte * cur_pte;
    struct pte * root = active_space_root();

    for(vma = USER_START_VMA; vma < USER_END_VMA; vma+=PAGE_SIZE ){ // for each page in the user region
        cur_pte = walk_pt(root, vma, 0); // walks the page table hierarchy to find the PTE for the specified virtual address
        if(cur_pte != NULL && cur_pte->flags & PTE_U){ // if the PTE is not NULL and the U bit is set
            memory_free_page(pagenum_to_pageptr(cur_pte->ppn)); // free the page
            cur_pte->flags &= ~PTE_V; // clear the V bit
            sfence_vma();   //Flush TLB
        }
    }
    sfence_vma(); //Flush TLB
    console_printf("Unmapped and freed all user pages\n");
}

int memory_validate_vptr_len (const void * vp, size_t len, uint_fast8_t rwxug_flags){
    // Input: const void*, size_t, uint_fast8_t
    // Output: int
    // Purpose: Validates that the address range vp, vp+len is accessible with the specified permissions. Returns 0 if the range is accessible, and an error otherwise.
    struct pte * cur_pte;
    struct pte * root = active_space_root();

    for(uintptr_t cur_vma = (uintptr_t)vp; cur_vma < (size_t)vp + len; cur_vma += PAGE_SIZE ){ // for each page in the range
        cur_pte = walk_pt(root, cur_vma, 0);    // walks the page table hierarchy to find the PTE for the specified virtual address
        if(cur_pte == NULL || !(cur_pte->flags & rwxug_flags))  // if the PTE is NULL or the flags don't match
            return -EACCESS;
    }
    return 0;
}

int memory_validate_vstr(const char * vs, uint_fast8_t ug_flags){
    // Input: const char*, uint_fast8_t
    // Output: int
    // Purpose: Validates that the string vs is accessible with the specified permissions. Returns 0 if the string is accessible, and an error otherwise.
    struct pte * cur_pte;
    uintptr_t cur_vma = (uintptr_t)vs;
    struct pte * root = active_space_root();
    uint16_t p_offset;
    uintptr_t pma;

    while(1){
        cur_pte = walk_pt(root, cur_vma, 0); // walks the page table hierarchy to find the PTE for the specified virtual address
        p_offset = POFFSET(cur_vma);    // get the offset
        if(cur_pte == NULL || !(cur_pte->flags & ug_flags)) // if the PTE is NULL or the flags don't match
            return -EACCESS;

        pma = (cur_pte->ppn)<<12 | p_offset; // get the physical memory address
        while(p_offset < PAGE_SIZE){    // for each byte in the page
            if(*(char *)pma == '\0')  // if the byte is null
                return 0;
            pma++;  // increment the physical memory address
            p_offset++; // increment the offset
            cur_vma++;  // increment the virtual memory address
        }        
    }
    return -EACCESS;
}

void memory_handle_page_fault(const void *vptr){
    // Input: const void*
    // Output: None
    // Purpose: Handles a page fault at the specified address. Either maps a page containing the faulting address, or calls process_exit().
    if((uintptr_t)vptr < USER_START_VMA || (uintptr_t)vptr > USER_END_VMA){ // if the address is outside of the user region
        panic("Page Fault - Accessed a page outside of user region!");
    }
    else{
        memory_alloc_and_map_page((uintptr_t)vptr, PTE_R | PTE_W | PTE_U); // allocates and maps a physical page
        sfence_vma();
    }
}

uintptr_t memory_space_clone(uint_fast16_t asid) { 
    // Input: uint_fast16_t 
    // Output: uintptr_t 
    // Purpose: Clones the active memory space. Returns the new memory space tag.

    struct pte *root = active_space_root(); // get the root page table
    struct pte *child_pt2 = (struct pte *)memory_alloc_page(); // allocate a physical page
    for (int i = 0; i < 3; i++) // for each page in the root
        child_pt2[i] = root[i]; // copy the page to the child
        
    for (uintptr_t vma = USER_START_VMA; vma < USER_END_VMA; vma += PAGE_SIZE) {
        struct pte* parent_pte0 = walk_pt(root, vma, 0);    //walk to the PTE for the parent
        if(parent_pte0 != NULL && parent_pte0->flags & PTE_V){  // if the PTE is not NULL and the Valid bit is set
            struct pte* child_pte0 = walk_pt(child_pt2, vma, 1); // walk to the PTE0 for the child
            void * child_curpage = memory_alloc_page();            // allocate a physical page
            void * parent_curpage = pagenum_to_pageptr(parent_pte0->ppn); // get the parent page ptr
            memcpy(child_curpage, parent_curpage, PAGE_SIZE);           // copy the parent page to the child page
            *child_pte0 = *parent_pte0;                             // set the child PTE0 to the parent PTE0    
            child_pte0->ppn = pageptr_to_pagenum(child_curpage);    // set the ppn of the child PTE0 to the child page
        }
    } 

    free_list = free_list->next; // free_list points to the next page in the list
    
    return ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) | pageptr_to_pagenum(child_pt2); // return the new memory space tag
}


//returns the pte in pt0 or NULL if create = 0 and the PTE doesn't exist
struct pte* walk_pt(struct pte* root, uintptr_t vma, int create) {
    // Input: struct pte*, uintptr_t, int
    // Output: struct pte*
    // Purpose: Walks the page table hierarchy to find the PTE for the specified virtual address. If create is true, creates any missing page tables.
    struct pte * pte2;
    struct pte * pte1;
    struct pte * pt1;
    struct pte * pt0;

    pte2 = &root[(VPN2(vma))];  // get the PTE for the second level page table
    if (pte2->flags & PTE_V) { // if the V bit is set
        pt1 = pagenum_to_pageptr(pte2->ppn); // get the page pointer
    } else if (create) { // if create is true
        pt1 = (struct pte*) memory_alloc_page(); // allocate a physical page
        *pte2 = ptab_pte(pt1, PTE_G); // set the PTE to the page table PTE
        sfence_vma();
    } else {
        return NULL;
    }
    pte1 = &pt1[VPN1(vma)]; // get the PTE for the first level page table
    if (pte1->flags & PTE_V) { // if the V bit is set
        pt0 = pagenum_to_pageptr(pte1->ppn); // get the page pointer
    } else if (create) { // if create is true
        pt0 = (struct pte*) memory_alloc_page(); // allocate a physical page
        *pte1 = ptab_pte(pt0, PTE_G); // set the PTE to the page table PTE
        sfence_vma();
    } else {
        return NULL;
    }
    return &pt0[VPN0(vma)];  // return the PTE for the specified virtual address
}

// INTERNAL FUNCTION DEFINITIONS
//

static inline int wellformed_vma(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

static inline int wellformed_vptr(const void * vp) {
    return wellformed_vma((uintptr_t)vp);
}

static inline int aligned_addr(uintptr_t vma, size_t blksz) {
    return ((vma % blksz) == 0);
}

static inline int aligned_ptr(const void * p, size_t blksz) {
    return (aligned_addr((uintptr_t)p, blksz));
}

static inline int aligned_size(size_t size, size_t blksz) {
    return ((size % blksz) == 0);
}

static inline uintptr_t active_space_mtag(void) {
    return csrr_satp();
}

//Usage: Use this function to convert a memory space tag (the satp reg) to a root page table pointer. (PTE struct)
static inline struct pte * mtag_to_root(uintptr_t mtag) {
    return (struct pte *)((mtag << 20) >> 8);
}


static inline struct pte * active_space_root(void) {
    return mtag_to_root(active_space_mtag());
}

//Usage: Use this function to convert a page number to a page pointer. This shift effectively multiplies the page number by the page size, converting it to a memory address.
static inline void * pagenum_to_pageptr(uintptr_t n) {
    return (void*)(n << PAGE_ORDER);
}

//Usage: Use this function to convert a page pointer to a page number. 
static inline uintptr_t pageptr_to_pagenum(const void * p) {
    return (uintptr_t)p >> PAGE_ORDER;
}

//Usage: Use this function to round up a pointer to the nearest higher multiple of a block size.
static inline void * round_up_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)(p + blksz-1) / blksz * blksz);
}

//Usage: Use this function to round up a virtual address to the nearest higher multiple of a block size.
static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz) {
    return ((addr + blksz-1) / blksz * blksz);
}

//Usage: Use this function to round up a size to the nearest higher multiple of a block size.
static inline size_t round_up_size(size_t n, size_t blksz) {
    return (n + blksz-1) / blksz * blksz;
}

//Usage: Use this function to round down a pointer to the nearest lower multiple of a block size.
static inline void * round_down_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)p / blksz * blksz);
}

//Usage: Use this function to round down a size to the nearest lower multiple of a block size.
static inline size_t round_down_size(size_t n, size_t blksz) {
    return n / blksz * blksz;
}

//Usage: Use this function to round down a virtual address to the nearest lower multiple of a block size.
static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz) {
    return (addr / blksz * blksz);
}
//Usage: Use this function to create a PTE that points to a leaf page. maps a virtual address to a physical page with the specified permissions.
static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags)
{
    return (struct pte) {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
        .ppn = pageptr_to_pagenum(pptr)
    };
}
//Usage: Use this function to create a PTE that points to another page table, enabling multi-level page table hierarchies.
static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag)
{
    return (struct pte) {
        .flags = g_flag | PTE_V,
        .ppn = pageptr_to_pagenum(ptab)
    };
}

static inline struct pte null_pte(void) {
    return (struct pte) { };
}

static inline void sfence_vma(void) {
    asm inline ("sfence.vma" ::: "memory");
}
