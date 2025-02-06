#include "io.h"
#include "string.h"
#include "fs.h"
#include "console.h"
#include "lock.h"
#include "heap.h"

// VARIABLE DECLARATIONS
#define MAX_FILES 32
boot_block_t boot_block;
file_t file_array[MAX_FILES];
struct io_intf* vioblk_io;
static struct lock FSLock;


// FUNCTION DECLARATIONS
// Input: io_intf* (io)
// Output: int (error code)
// Purpose: Takes an io intf* to the filesystem provider and 
// sets up the filesystem for future fs open operations
int fs_mount(struct io_intf* io) {
    if (io == NULL) {       // checks if a valid io_interface has been provided
        return -EINVAL;     // returns invalid if we have a null ptr
    }
    ioread(io, &boot_block, sizeof(boot_block_t));      // reads the boot block into our global variable

    console_printf("Number of Dentries: %d\n", boot_block.num_dentry);     
    console_printf("Number of Inodes: %d\n", boot_block.num_inodes);
    console_printf("Number of Data Blocks: %d\n", boot_block.num_data);
    vioblk_io = io;     // intializes our gloval variable vioblk_io
    for(int i = 0; i < MAX_FILES; i++) {
        file_array[i].flag = 0;     // sets the flags for each of our file structs to 0
        file_array[i].io.refcnt = 0;    // sets the refcnt for each of our file structs to 0
    }

    //Initialize the lock
    lock_init(&FSLock, "KFSLock");
    
    trace("File Mount Successful!\n"); 
    return 0;       // return 0 if everything was successful
}

// Input: char* (name), io_intf** (io)
// Output: int (error code)
// Purpose: Takes the name of the file to be opened and modifies the given pointer to contain 
// the io intf of the file. This function should also associate a specific file struct with 
// the file and mark it as in-use
int fs_open(const char* name, struct io_intf** io) {
    int i,j,error;
    dentry_t dentry;
    // creates an fs_ops struct that will have how our file can read,write, etc.
    console_printf("Thread <%s> trying to open file %s and acquire KFS Lock\n", thread_name(running_thread()), name);
    lock_acquire(&FSLock);   //  Acquire the Read/Write Lock before opening
    static const struct io_ops fs_ops = {   
		.close = fs_close,      // sets .close interface to fs one
		.read = fs_read,        // sets .read interface to fs one
		.write = fs_write,      // sets .write interface to fs one
        .ctl = fs_ioctl         // sets .ctl interface to fs one
    };
   
    for(i = 0; i < boot_block.num_dentry; i++) {        // iterate through number of dentries to find file
        if (strcmp(name, boot_block.dir_entries[i].file_name) == 0) {       // compares file name
            dentry = boot_block.dir_entries[i];     // gets the corresponding dentry
            break;
        }
    }
    for(j = 0; j < MAX_FILES; j++) {       // finds an empty slot in the file struct array
        if(file_array[j].flag == 0) {   // if the flag is 0 exit (flag 0 means file is not open)
            break;
        }
    }
    if(i == boot_block.num_dentry || j == MAX_FILES) {    // checks if we didnt find file or too many files or open
        return -EBADFMT;    // returns bad file management error code
    }
    file_array[j].io.ops = &fs_ops;     // Set io ops to the fs_ops struct created earlier
    file_array[j].inode = dentry.inode;     // Set the inode to the corresponding inode number
    file_array[j].file_pos = 0;     // Set the file position initally to 0
    file_array[j].flag = 1;     // Set the file flag to 1 to indicate it is open
    *io = &file_array[j].io;    // Sets the given pointer to the io interfance for the file
    file_array[j].io.refcnt += 1; 
    uint32_t offset = (file_array[j].inode + 1)*FS_BLKSZ;       // calculates the offset to the file's byte_len
    error = ioseek(vioblk_io, offset);      // sets the vioblk position to the offset
    if(error < 0) {
            lock_release(&FSLock);
            return error;     
    }
    error = ioread(vioblk_io, &file_array[j].byte_len, sizeof(uint32_t));      // reads 4 byte for the file's byte length
    if(error < 0) {
            lock_release(&FSLock);
            return error;     
    }
    trace("File was successfully opened and initialized!");
    lock_release(&FSLock);   // Release the read/write lock after opening
    console_printf("Thread <%s> finished opening file %s and released KFS Lock\n", thread_name(running_thread()), name);
    return 0;   
}
// Input: io_intf* (io)
// Output: none
// Purpose: Closes a file by setting the flag to 0
void fs_close(struct io_intf* io) {
    file_t* close_file = (file_t*) io;      // gets a pointer to the file we want to close
    io->refcnt -= 1;
    if(io->refcnt == 0){
        close_file->flag = 0;       // sets the flag to 0 to indicate that file is closed
        trace("Successfully closed file!\n"); 
    }
    else
        trace("File not yet closed, other references still exist\n");
    return;
}
// Input: io_intf* (io), void* (buf), unsigned long (n)
// Output: none
// Purpose: Writes n bytes from buf into the file associated with io. 
// Updates metadata in the file struct as appropriate 
long fs_write(struct io_intf* io, const void* buf, unsigned long n) {
    if(io == NULL) {        // checks if the pointer is received
        return -EINVAL;     // check error code
    }
    file_t* my_file = (file_t*) io;     // gets the file pointer
    lock_acquire(&FSLock);   //  Acquire the Read/Write Lock before writing
    uint64_t f_pos = my_file->file_pos;     // gets the file position
    // console_printf("File Pos: %d\n", my_file->file_pos);
    uint64_t f_len = my_file->byte_len;     // gets the file length
    uint64_t inode = my_file->inode;        // gets the inode number
    uint32_t db_start, db_end, db_pos, num_write;
    db_start = (uint32_t)(f_pos / FS_BLKSZ);        // finds the starting data block
    db_end = (uint32_t)((f_pos + n) / FS_BLKSZ);    // finds the ending data block
    num_write = 0;      // number of bytes written
    db_pos = f_pos % FS_BLKSZ;      // find the position within a data block 
    if((int)(f_pos + n) > f_len) {      // checks if the file position + n we want to write is greater than file length
        n = f_len-f_pos;    // if it is sets the number of bytes to write to n
    }
    uint64_t inode_addr = (inode+1) * FS_BLKSZ;     // finds the address of the inode
    uint32_t db_num;
    uint64_t db_addr;
    int error; 
    for(int i = db_start; i <= db_end; i++) {       // starts the data block start
        error = ioseek(vioblk_io, inode_addr+sizeof(db_num)*(i+1));      // sets the vioblk position to inode address
        if(error < 0) {
            lock_release(&FSLock);
            return error;     // if the ioseek fails return an error
        }
        error = ioread(vioblk_io, &db_num, sizeof(db_num));      // reads from the inode offset address (gets the datablock num)
        if(error < 0) {
            lock_release(&FSLock);
            return error;     
        }
       
        db_addr = (boot_block.num_inodes + db_num + 1) * FS_BLKSZ;  // calculates the address of the data block
        error = ioseek(vioblk_io, db_addr + db_pos);        //  set the position to the address of the datablock
        if(error < 0) {
            lock_release(&FSLock);
            return error;     
        }
        if ((db_pos + n) <= FS_BLKSZ){      // checks if the amount of we want to write exceeds the blksz
            error = iowrite(vioblk_io, buf + num_write, n);     // write n bytes to correct location in buf
            if(error < 0) {
                lock_release(&FSLock);
                return error;     
            }
            num_write += n;     // increase the number num_write
            my_file->file_pos += num_write;     // increase the file position by the number of bytes written
            trace("Successfully wrote %d bytes to file position!", num_write);     
            lock_release(&FSLock);
            return num_write;       // return number written
        }
        else {
            error = iowrite(vioblk_io, buf + num_write, FS_BLKSZ - db_pos);      // write n bytes to correct location in buf
            if(error < 0) {
                lock_release(&FSLock);
                return error;     
            }
            num_write += (FS_BLKSZ - db_pos);       // increase the number num_write
            db_pos = 0;     // reset the db pos
            n -= (FS_BLKSZ - db_pos);       // decrease the amount of bytes remaining to right
        }
    }
    trace("Successfully wrote %d bytes to file position!", num_write);
    lock_release(&FSLock);   // Release the read/write lock after writing
    return num_write;       // return number written
}
// Input: io_intf* (io), void* (buf), unsigned long (n)
// Output: none
// Purpose: Reads n bytes from buf into the file associated with io. 
// Updates metadata in the file struct as appropriate 
long fs_read(struct io_intf* io, void* buf, unsigned long n) {
    if(io == NULL) {        // checks if the pointer provided is null
        return -EINVAL; // check error code
    }
    file_t* my_file = (file_t*) io;     // sets up file struct pointer
    uint64_t f_pos = my_file->file_pos;     // store file position
    uint64_t f_len = my_file->byte_len;     // store byt elength
    uint64_t inode = my_file->inode;        // store inode number
    uint32_t db_start, db_end, db_pos, num_read;
    db_start = (uint32_t)(f_pos / FS_BLKSZ);        // find the start data block
    db_end = (uint32_t)((f_pos + n) / FS_BLKSZ);    // find the ending data block   
    num_read = 0;       // number of bytes read
    
    db_pos = f_pos % FS_BLKSZ;      // set up variable to represent the position within a data block
    if((int)(f_pos + n)> f_len) {       // check if the amount we want to read exceeds file length
        n = f_len-f_pos;        // redefine the number we want to write
    }
    
    uint64_t inode_addr = (inode+1) * FS_BLKSZ; // calculates the inode address offset
    uint32_t db_num;
    uint64_t db_addr;
    int error; 
    // check if current thread already has the lock
    lock_acquire(&FSLock);   //  Acquire the Read/Write Lock before reading
    for(int i = db_start; i <= db_end; i++) {   // runs from the data block start to data block end 
        error = ioseek(vioblk_io, inode_addr+(i+1)*sizeof(db_num));     // set position within vioblock
        if(error < 0) {
            lock_release(&FSLock);
            return error;     
        }
        error = ioread(vioblk_io, &db_num, sizeof(db_num));     // read the data block number
        if(error < 0) {
            lock_release(&FSLock);
            return error;     
        }
        
        db_addr = (boot_block.num_inodes + db_num + 1) * FS_BLKSZ;      // calculates the offset for data block addr
        error = ioseek(vioblk_io, db_addr + db_pos);        // set position within vioblock to data block addr
        if(error < 0) {
            lock_release(&FSLock);
            return error;     
        }
        if ((db_pos + n) <= FS_BLKSZ){      // check if amount we are trying to read exceeds the block size
            error = ioread(vioblk_io, buf + num_read, n);   // read from address
            if(error < 0) {
                lock_release(&FSLock);
                return error;     
            }
            num_read += n;      // increment the number of bytes read
            my_file->file_pos += num_read;      // increment the file_pos   
            trace("Successfully read file!"); 
            lock_release(&FSLock);
            return num_read;    // return num read
        }
        else {
            error = ioread(vioblk_io, buf + num_read, FS_BLKSZ - db_pos);       // read from address
            if(error < 0) {
                lock_release(&FSLock);
                return error;     
            }
            num_read += (FS_BLKSZ - db_pos);        // increment number of bytes read
            db_pos = 0;     // set dbpos to 0 for the next block
            n -= (FS_BLKSZ - db_pos);       // decrement the number of bytes left to read
        }
    }
    trace("Successfully read file!"); 
    lock_release(&FSLock);   // Release the read/write lock after reading
    return num_read;    // return num read
}

// Input: io_intf* (io), int (cmd), void* (arg)
// Output: int (error)
// Purpose: Performs a device-specific function based on cmd.
int fs_ioctl(struct io_intf* io, int cmd, void* arg) {
    // TODO please add locking
    lock_acquire(&FSLock);   //  Acquire the Read/Write Lock before performing ioctl
    int result;
    if(io == NULL) {
        return -EINVAL; // check error code
    }
    switch(cmd) {
        case(IOCTL_GETLEN): // if we are trying to get length run fs_getlen
            result = fs_getlen((file_t*) io, arg);
            break;
        case(IOCTL_GETPOS): // if we are trying to get pos run fs_getpos
            result = fs_getpos((file_t*) io, arg);
            break;
        case(IOCTL_SETPOS): // if we are trying to set pos run fs_setpos
            result = fs_setpos((file_t*) io, arg);
            break;
        case(IOCTL_GETBLKSZ):   // if we are trying to get blk size run fs_getblsz
            result = fs_getblksz((file_t*) io, arg);
            break;
        default:    // return 
            result = -EINVAL;
    }
    lock_release(&FSLock);   // Release the read/write lock after performing ioctl
    return result;  // return result
}

// Input: file_t* (fd), void* (arg)
// Output: int (error)
// Purpose: gets the file length
int fs_getlen(file_t* fd, void* arg) {
    // think about possible errors and boundaries
    if(fd == NULL) {    // check if file_t pointer is null
        return -ENOENT; // check error code
    }
    *((uint64_t*)arg) = fd->byte_len;   // set value to byte length
    return 0;       // return 0 if successful
}
// Input: file_t* (fd), void* (arg)
// Output: int (error)
// Purpose: returns the position of the "cursor" in the file 
int fs_getpos(file_t* fd, void* arg) {
    // think about possible errors and boundaries
    if(fd == NULL) {    // checks if the file pointer is null
        return -ENOENT; // returns invalid
    }
    *((uint64_t*)arg) = fd->file_pos;   // gets position
    return 0;   // return 0 if successfull
}   
// Input: file_t* (fd), void* (arg)
// Output: int (error)
// Purpose: sets the position of the "cursor" in the file 
int fs_setpos(file_t* fd, void* arg) {  
    if(fd == NULL) {    // checks if the file pointer is null
        return -ENOENT; // returns invalid
    }
    fd->file_pos = *((uint64_t*) arg);  // sets the file pos
    return 0;   // return 0 if successful
}
// Input: file_t* (fd), void* (arg)
// Output: int (error)
// Purpose: returns the size of blocks in file system
int fs_getblksz(file_t* fd, void* arg) { 
    if(fd == NULL) {    // checks if file pointer is null
        return -ENOENT;     // returns invalid
    }
    *((uint64_t*)arg) = FS_BLKSZ;   // gets the block size

    return 0;   // return 0 if successful
}