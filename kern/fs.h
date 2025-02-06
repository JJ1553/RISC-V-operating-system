//           fs.h - File system interface
//          

#ifndef _FS_H_
#define _FS_H_

#include "io.h"

// MACROS
#define IOCTL_GETLEN        1
#define IOCTL_SETLEN        2
#define IOCTL_GETPOS        3
#define IOCTL_SETPOS        4
#define IOCTL_FLUSH         5
#define IOCTL_GETBLKSZ      6

#define MAXFLOPEN 32
#define MAXFL 63
#define FS_BLKSZ      4096
#define FS_NAMELEN    32
extern char fs_initialized;

// struct for dentry (directory entries)
typedef struct dentry_t{
    char file_name[FS_NAMELEN];
    uint32_t inode;
    uint8_t reserved[28];
}__attribute((packed)) dentry_t; 

//struct containing the boot block structure
typedef struct boot_block_t{
    uint32_t num_dentry;
    uint32_t num_inodes;
    uint32_t num_data;
    uint8_t reserved[52];
    dentry_t dir_entries[63];
}__attribute((packed)) boot_block_t;

// struct for the inodes
typedef struct inode_t{
    uint32_t byte_len;
    uint32_t data_block_num[1023];
}__attribute((packed)) inode_t;

// struct for data blocks
typedef struct data_block_t{
    uint8_t data[FS_BLKSZ];
}__attribute((packed)) data_block_t;

// struct for the file_t
typedef struct file_t {
    struct io_intf io;
    uint64_t file_pos;
    uint64_t byte_len;
    uint64_t inode;
    uint64_t flag;
} file_t;

extern void fs_init(void);
extern int fs_mount(struct io_intf * blkio);
extern int fs_open(const char * name, struct io_intf ** ioptr);
long fs_write(struct io_intf* io, const void* buf, unsigned long n);
long fs_read(struct io_intf* io, void* buf, unsigned long n);
void fs_close(struct io_intf* io);
int fs_ioctl(struct io_intf* io, int cmd, void* arg);
int fs_getlen(file_t* fd, void* arg);
int fs_getpos(file_t* fd, void* arg);
int fs_setpos(file_t* fd, void* arg);
int fs_getblksz(file_t* fd, void* arg);



//           _FS_H_
#endif
