// syscall.c - System call handlers

#include "syscall.h"
#include "process.h"
#include "io.h"
#include "thread.h"
#include "device.h"
#include "console.h"
#include "scnum.h"
#include "memory.h"
#include "error.h"
#include "fs.h"
#include "timer.h"


void sys_exit(void) {
    //inputs: none
    //outputs: none
    //description: Exit the current process by calling process_exit.
    process_exit(); // call process_exit
}


void sys_msgout(const char * msg) {
    //inputs: msg - message to output
    //outputs: none
    //description: Output a message to the console using console function puts.
    if(memory_validate_vstr(msg, PTE_U) != 0) { // validate the message
        return;
    }
    console_puts(msg); // call console_puts
    //console_printf("Thread: %d\n", running_thread());
    
    /* SLIDES IMPLEMENTATION
    int sysmsgout(const char * msg) {
        int result;
        trace("%s(msg=%p)", __func__, msg);
        result = memory_validate_vstr(msg, PTE_U);
        if (result != 0)
            return result;
        kprintf("Thread <%s:%d> says: %s\n",
        thread_name(running_thread()),
        running_thread(), msg);
        return 0;
    }
    */
}

// System call handler for opening a device
int sys_devopen(int fd, const char * name, int instno) {
    //inputs: fd - file descriptor, name - device name, instno - instance number
    //outputs: file descriptor on success, negative error code on error
    //description: Open a device by calling device_open through the io object.

    if(fd < 0) {
        for(int i = 0; i < PROCESS_IOMAX; i++) { // iterate through the iotab array
            if(current_process()->iotab[i] == NULL) { // iterate through the iotab array
                fd = i; // find the next available file descriptor
                break;
            }
        }
    }


    int ret = device_open(&current_process()->iotab[fd], name, instno); // call device_open with the io object

    if(ret < 0) {
        return ret;
    }

    return fd;
}


int sys_fsopen(int fd, const char * name) {
    //inputs: fd - file descriptor, name - file name
    //outputs: file descriptor on success, negative error code on error
    //description: Open a file in the file system by calling fs_open through the io object.

    if(fd < 0) {
        for(int i = 0; i < PROCESS_IOMAX; i++) { // iterate through the iotab array
            if(current_process()->iotab[i] == NULL) { 
                fd = i;                         // find the next available file descriptor
                break;
            }
        }
    }

    int ret = fs_open(name, &current_process()->iotab[fd]); // call fs_open with the io object

    if(ret < 0) {
        return ret;
    }

    return fd;
}


void sys_close(int fd) {
    //inputs: fd - file descriptor
    //outputs: none
    //description: Close the file descriptor by calling ioclose.
    if(fd < 0 || current_process()->iotab[fd] == NULL) { // check if the file descriptor is valid
        return;
    }
    ioclose(current_process()->iotab[fd]); // call ioclose with the io object
    current_process()->iotab[fd] = NULL;  // closes opened io object, set iotab[fd] = NULL

}


long sys_read(int fd, void * buf, size_t bufsz) {
    //inputs: fd - file descriptor, buf - buffer, bufsz - buffer size
    //outputs: number of bytes read on success, 0 on end of file, negative error code on error
    //description: Read bufsz bytes from the file descriptor into the buffer by calling ioread.
    int validate_result = memory_validate_vptr_len(buf, bufsz, PTE_W | PTE_U);
    if (validate_result != 0) return validate_result;
    if(fd < 0 || current_process()->iotab[fd] == NULL) { // check if the file descriptor is valid
        return -EINVAL;
    }
    return ioread(current_process()->iotab[fd], buf, bufsz);
}


long sys_write(int fd, const void * buf, size_t len) {
    //inputs: fd - file descriptor, buf - buffer, len - length
    //outputs: number of bytes written on success, 0 on end, neg on error
    //description: Write len bytes from the buffer to the file descriptor by calling iowrite.
    int validate_result = memory_validate_vptr_len(buf, len, PTE_R | PTE_U);
    if (validate_result != 0) return validate_result;
    if(fd < 0 || current_process()->iotab[fd] == NULL) { // check if the file descriptor is valid
        return -EINVAL;
    }
    return iowrite(current_process()->iotab[fd], buf, len);
}

int sys_ioctl(int fd, int cmd, void * arg) {
    //inputs: fd - file descriptor, cmd - command, arg - argument
    //outputs: 0 on success, negative error code on error
    //description: Perform an I/O control operation on the file descriptor by calling ioctl.
    if(fd < 0 || current_process()->iotab[fd] == NULL) { // check if the file descriptor is valid
        return -EINVAL;
    }
    return ioctl(current_process()->iotab[fd], cmd, arg);
}

int sys_exec(int fd) {
    //inputs: fd - file descriptor
    //outputs: 0 on success, negative error code on error
    //description: Execute a process by calling process_exec. Close the io object m and zero iotab[fd] since the io object will be closed.

    if(current_process()->iotab[fd] == NULL) { // check if the file descriptor is valid
        return -EINVAL;
    }
    int ret = process_exec(current_process()->iotab[fd]); //call process_exec with the io object
    current_process()->iotab[fd] = NULL; //close the io object

    return ret;
}

int sys_wait(int tid){
    //inputs: tid - thread id
    //outputs: thread ID of child that exited
    //description: wait for a child thread to exit by calling thread_join

    if(tid == 0)
        return thread_join_any();
    else
        return thread_join(tid);
}

void sys_usleep(unsigned long us){
    //inputs: us - microseconds
    //outputs: none
    //description: sleep for us microseconds

    struct alarm al;
    const char *name = "usleep";

    alarm_init(&al, name);  // call alarm_init
    alarm_sleep_us(&al, us);    // call alarm_sleep_us
    alarm_reset(&al);       //call alarm_reset
}

static int sys_fork(const struct trap_frame *tfr){
    // inputs: tfr - trap frame
    // outputs: 0 on success, negative error code on error
    // description: Fork the current process by calling process_fork
    int ret = process_fork(tfr); // call process_fork
    return ret;
}

void syscall_handler(struct trap_frame *tfr) {
    //inputs: trap frame
    //outputs: none
    //description: System call handler. Calls the appropriate system call handler based on the system call number in the trap frame.
    
    tfr->sepc += 4; // increment the program counter by 4 bytes for 32 bit system
    

    const uint64_t * const a = tfr->x; // a is a pointer to the system call number in the trap frame

    // Switch statement to handle different system calls based on the system call number
    switch(a[TFR_A7]) {
        case SYSCALL_EXIT:
            //process exit system call
            sys_exit();
            break;
        case SYSCALL_MSGOUT:
            //message output system call
            sys_msgout((const char *)a[TFR_A0]);
            break;
        case SYSCALL_CLOSE:
            //file descriptor close system call
            sys_close((int)a[TFR_A0]);
            break;
        case SYSCALL_READ:
            //read from file descriptor system call
            tfr->x[TFR_A0] = sys_read((int)a[TFR_A0], (void *)a[TFR_A1], (size_t)a[TFR_A2]);
            break;
        case SYSCALL_WRITE:
            //write to file descriptor system call
            tfr->x[TFR_A0] = sys_write((int)a[TFR_A0], (const void *)a[TFR_A1], (size_t)a[TFR_A2]);
            break;
        case SYSCALL_IOCTL:
            //I/O control operation system call
            tfr->x[TFR_A0] = sys_ioctl((int)a[TFR_A0], (int)a[TFR_A1], (void *)a[TFR_A2]);
            break;
        case SYSCALL_DEVOPEN:
            //device open system call
            tfr->x[TFR_A0] = sys_devopen((int)a[TFR_A0], (const char *)a[TFR_A1], (int)a[TFR_A2]);
            break;
        case SYSCALL_FSOPEN:
            //file system open system call
            tfr->x[TFR_A0] = sys_fsopen((int)a[TFR_A0], (const char *)a[TFR_A1]);
            break;
        case SYSCALL_EXEC:
            //process execution system call
            tfr->x[TFR_A0] = sys_exec((int)a[TFR_A0]);
            break;
        case SYSCALL_FORK:
            //process fork system call
            tfr->x[TFR_A0] = sys_fork(tfr);
            break;
        case SYSCALL_WAIT:
            //process wait system call
            tfr->x[TFR_A0] = sys_wait((int)a[TFR_A0]);
            break;
        case SYSCALL_USLEEP:
            //process usleep system call
            sys_usleep((unsigned long)a[TFR_A0]);
            break;
        default:
            tfr->x[TFR_A0] = -ENOTSUP; // Return error for unknown system call
            break;
    }
    
}
