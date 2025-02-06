#include <stddef.h>
#include "trap.h"

extern void sys_exit(void);
extern void sys_msgout(const char * msg);
extern int sys_devopen(int fd, const char * name, int instno);
extern int sys_fsopen(int fd, const char * name);
extern void sys_close(int fd);
extern long sys_read(int fd, void * buf, size_t bufsz);
extern long sys_write(int fd, const void * buf, size_t bufsz);
extern int sys_ioctl(int fd, int req, void * arg);
extern int sys_exec(int pid);
void syscall_handler(struct trap_frame *tfr);
extern int sys_wait(int tid);
extern void sys_usleep(unsigned long us);
