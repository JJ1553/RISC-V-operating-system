// process.c - user process
//

#include "process.h"
#include "thread.h"
#include "memory.h"
#include "elf.h"
#include "halt.h"
#include "heap.h"

#ifdef PROCESS_TRACE
#define TRACE
#endif

#ifdef PROCESS_DEBUG
#define DEBUG
#endif


// COMPILE-TIME PARAMETERS
//

// NPROC is the maximum number of processes

#ifndef NPROC
#define NPROC 16
#endif

// INTERNAL FUNCTION DECLARATIONS
//

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_PID 0

// The main user process struct

static struct process main_proc;

// A table of pointers to all user processes in the system

struct process * proctab[NPROC] = {
    [MAIN_PID] = &main_proc
};

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

void procmgr_init(void) {
    //inputs: none
    //outputs: none
    //description: Initialize the process manager. sets up the main process struct to main process/mtag/thread, and initializes the iotab array to NULL.
    
    if (procmgr_initialized) {
        return;
    }

    main_proc.id = MAIN_PID; // set the process id of the main process to MAIN_PID
    main_proc.tid = running_thread(); // set the thread id of the main process to the running thread

    thread_set_process(main_proc.tid, &main_proc); // set the process of the main thread to the main process
    main_proc.mtag = active_memory_space(); // set the memory space to the active memory space

    // Initialize the iotab array to NULL
    for (int i = 0; i < PROCESS_IOMAX; i++) { // iterate through the iotab array
        main_proc.iotab[i] = NULL; // set the io object to NULL
    }

    procmgr_initialized = 1; //  set procmgr_initialized to 1

}

int process_exec(struct io_intf * exeio) {
    //inputs: exeio - I/O interface from which to load the ELF file
    //outputs: 0 on success, negative error code on error
    //description: Load an ELF file into memory and execute it
    void (*eentry)(void);
    uintptr_t usp = USER_STACK_VMA; // user stack pointer
    // uintptr_t upc;

    memory_unmap_and_free_user(); // unmaps and frees all pages with the U bit set in the PTE flags

    // Load the ELF file into memory
    int err = elf_load(exeio, &eentry); // load the ELF file into memory
    if(err < 0) {
        console_printf("elf_load failed\n");
        return err;
    }
    console_printf("hello there yipeeeee\n");

    thread_jump_to_user(usp, (uintptr_t) eentry); // jump to the user space
}

void process_terminate(int pid) {
    //inputs: pid - process id
    //outputs: none
    //description: Terminate the process with the given id. This involves terminating the thread associated with the process, reclaiming the memory space, and freeing the process struct.

    struct process * proc = proctab[pid]; // get the process struct associated with the process id

    if (proc == NULL) {
        return;
    }

    // Terminate the thread associated with the process
    // int tid = proc->tid;
    // recycle_thread(tid);


    // Close all open I/O devices
    for (int i = 0; i < PROCESS_IOMAX; i++) { // iterate through the iotab array
        if (proc->iotab[i] != NULL) { // if the io object is not NULL
            ioclose(proc->iotab[i]); // close the io object
            proc->iotab[i] = NULL; // set the io object to NULL
        }
    }

    // Reclaim the memory space

    if (proc->mtag != active_memory_space()) { // if the memory space is not the active memory space
        memory_space_reclaim(); // reclaim the memory space
    }

    // Free the process struct

    proctab[pid] = NULL; // set the process struct to NULL
    
}

void process_exit(void) {
    //inputs: none
    //outputs: none
    //description: Terminate the current process and exit the current thread.

    int pid = current_pid(); // get the process id of the current process
    process_terminate(pid); // terminate the process
    thread_exit(); // exit the current thread
}

int process_fork(const struct trap_frame *tfr){
    //inputs: tfr - trap frame
    //outputs: 0 on success, negative error code on error
    //description: Fork the current process. This involves creating a new process struct, copying the I/O devices from the parent process, and cloning the memory space.
    struct process* child = kmalloc(sizeof(struct process)); // allocate memory for the child process

    int i;
    for(i=0; i<NPROC; i++){ // iterate through the proctab array
        if(proctab[i] == NULL){ // if the process struct is NULL
            proctab[i] = child; //  set the process struct to the child process
            child->id = i; //   set the process id to i
            break;
        }
    }
    if(i >= NPROC)
        panic("Maximum number of processes surpassed!");

    struct process * parent = current_process(); // get the process struct of the current process
    for(int i=0; i<PROCESS_IOMAX; i++){ // iterate through the iotab array
        if(parent->iotab[i] != NULL){ // if the io object is not NULL
            child->iotab[i] = parent->iotab[i]; // set the io object to the parent io object
            child->iotab[i]->refcnt += 1; // increment the reference count of the io object
        }
    }
    child->mtag = memory_space_clone(parent->mtag);     // clone the memory space of the parent process
    return thread_fork_to_user(child, tfr); // fork the thread to the user space
}