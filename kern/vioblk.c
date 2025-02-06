//           vioblk.c - VirtIO serial port (console)
//          

#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "plic.h"
#include "lock.h"

//           COMPILE-TIME PARAMETERS
//          

#define VIOBLK_IRQ_PRIO 1

//           INTERNAL CONSTANT DEFINITIONS
//          

//           VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

//           INTERNAL TYPE DEFINITIONS
//          

//           All VirtIO block device requests consist of a request header, defined below,
//           followed by data, followed by a status byte. The header is device-read-only,
//           the data may be device-read-only or device-written (depending on request
//           type), and the status byte is device-written.

struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

//           Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

//           Status byte values

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

//           Main device structure.
//          
//           FIXME You may modify this structure in any way you want. It is given as a
//           hint to help you, but you may have your own (better!) way of doing things.

struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    struct io_intf io_intf;
    uint16_t instno;
    uint16_t irqno;
    int8_t opened;
    int8_t readonly;

    //           optimal block size
    uint32_t blksz;
    //           current position
    uint64_t pos;
    //           sizeo of device in bytes
    uint64_t size;
    //           size of device in blksz blocks
    uint64_t blkcnt;

    struct {
        //           signaled from ISR
        struct condition used_updated;

        //           We use a simple scheme of one transaction at a time.

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        //           The first descriptor is an indirect descriptor and is the one used in
        //           the avail and used rings. The second descriptor points to the header,
        //           the third points to the data, and the fourth to the status byte.

        struct virtq_desc desc[4];
        struct vioblk_request_header req_header;
        uint8_t req_status;
    } vq;

    //           Block currently in block buffer
    uint64_t bufblkno;
    //           Block buffer
    char * blkbuf;

    //          Lock
    struct lock RWLock;
};

//           INTERNAL FUNCTION DECLARATIONS
//          

static int vioblk_open(struct io_intf ** ioptr, void * aux);

static void vioblk_close(struct io_intf * io);

static long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz);

static long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n);

static int vioblk_ioctl (
    struct io_intf * restrict io, int cmd, void * restrict arg);

static void vioblk_isr(int irqno, void * aux);

//           IOCTLs

static int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr);
static int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr);
static int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr);
static int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr);

//           EXPORTED FUNCTION DEFINITIONS
//          

//           Attaches a VirtIO block device. Declared and called directly from virtio.c.
/*
vioblk_attach attaches the argument regs to a new device. It negotiates feateures with this device,
initializes the i/o ops, sets feature bits, fills our descriptors, attaches virtqueues, and registers
both the irq and the device to be able to be opened and used
inputs: volatile struct virtio_mmio_regs * regs, irqno
outputs: none
*/
void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    //           FIXME add additional declarations here if needed
    #define VQ_NUM 0
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device * dev;
    uint_fast32_t blksz;
    int result;

    assert (regs->device_id == VIRTIO_ID_BLOCK);

    //           Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    //           fence o,io
    __sync_synchronize();

    //           Negotiate features. We need:
    //            - VIRTIO_F_RING_RESET and
    //            - VIRTIO_F_INDIRECT_DESC
    //           We want:
    //            - VIRTIO_BLK_F_BLK_SIZE and
    //            - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    //           If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    debug("%p: virtio block device block size is %lu", regs, (long)blksz);

    //           Allocate initialize device struct

    dev = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(dev, 0, sizeof(struct vioblk_device));

    //           FIXME Finish initialization of vioblk device here

    //          Initialize + Link the necessary IO Operations
    static const struct io_ops virtio_ops = {
        .close = vioblk_close,
        .read = vioblk_read,
        .write = vioblk_write,
        .ctl = vioblk_ioctl
    };

    //          Set necessary device fields
    dev->io_intf.ops = &virtio_ops;
    dev->io_intf.refcnt = 0;
    dev->regs = regs;
    dev->irqno = irqno;
    dev->blksz = blksz;
    dev->size = regs->config.blk.capacity;
    dev->blkcnt = dev->size / blksz;
    dev->blkbuf = (char *)dev + sizeof(struct vioblk_device);
    console_printf("\nAttaching Device Parameters: \n");
    console_printf("Interrupt Request No: %u\n", irqno);
    console_printf("Block Size: %u\n", blksz);
    console_printf("Device Size: %u\n", dev->size);
    console_printf("Number of Blocks: %u\n", irqno);

    __sync_synchronize();

    //          Fill out descriptors: indirect, header, data, and status
    //          Indirect descriptor
    dev->vq.desc[0].addr = (uint64_t)&dev->vq.desc[1];
    dev->vq.desc[0].len = 3 * sizeof(struct virtq_desc);  //3 comes from the 3 other descriptors
    dev->vq.desc[0].flags = VIRTQ_DESC_F_INDIRECT;
    dev->vq.desc[0].next = 0;
    
    //          Header descriptor
    dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header;
    dev->vq.desc[1].len = sizeof(struct vioblk_request_header);
    dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT;
    dev->vq.desc[1].next = 1;

    //          Data descriptor
    dev->vq.desc[2].addr = (uint64_t)dev->blkbuf;
    dev->vq.desc[2].len = dev->blksz;
    dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT;
    dev->vq.desc[2].next = 2;

    //          Status Descriptor
    dev->vq.desc[3].addr = (uint64_t)&dev->vq.req_status;
    dev->vq.desc[3].len = sizeof(dev->vq.req_status);
    dev->vq.desc[3].flags = VIRTQ_DESC_F_WRITE;
    dev->vq.desc[3].next = 0;


    //           attach avail and used virtq's using virtio_attach_virtq
    dev->vq.avail.idx = 0;
    dev->vq.avail.flags &= ~VIRTQ_AVAIL_F_NO_INTERRUPT;
    virtio_attach_virtq(regs, VQ_NUM, 1, (uint64_t)(uintptr_t)dev->vq.desc, (uint64_t)&dev->vq.used,(uint64_t) &dev->vq.avail);

    //          Initialize the lock
    lock_init(&dev->RWLock, "VIOLock");

    //register the ISR
    intr_register_isr(irqno, VIOBLK_IRQ_PRIO, vioblk_isr, dev);

    //          Initialize the used_updated condition
    condition_init(&dev->vq.used_updated, "used_updated");

    //          Register the device, making dev the aux
    device_register("blk", &vioblk_open, dev);

    regs->status |= VIRTIO_STAT_DRIVER_OK;    
    //           fence o,oi
    __sync_synchronize();
}

/*
This function opens the device to be able to be used. Specifically the virtqueues are enabled as
well as the interrupts and the plic interrupts for the thread. The aux input is our device.
inputs: struct io_intf ** ioptr, void * aux
outputs: int (success/error code)
*/
int vioblk_open(struct io_intf ** ioptr, void * aux) {
    //           FIXME your code here

    //          Get pointer to the device based on the ioptr
    struct vioblk_device * const dev = (struct vioblk_device *) aux;
    *ioptr = &dev->io_intf;
    dev->io_intf.refcnt += 1;  //Increment reference counter
    if(dev->opened) {
        return -EBUSY;
    }

    virtio_enable_virtq(dev->regs, VQ_NUM);

    plic_enable_irq(dev->irqno, VIOBLK_IRQ_PRIO);

    //          Enable interrupts
    intr_enable_irq(dev->irqno);

    //          Set necessary flags
    dev->opened = 1;

    console_printf("\nVioblk Open Successful! checks:\n");
    console_printf("Check queue_ready bit == 1: %u\n", dev->regs->queue_ready == 1);
    console_printf("Check opened bit == 1: %u\n", dev->opened == 1);

    dev->io_intf.refcnt += 1;  //Increment reference counter

    return 0; //success

}

//           Must be called with interrupts enabled to ensure there are no pending
//           interrupts (ISR will not execute after closing).

/*
This function closes the device, resetting the virtqueues and disabling the irqs
inputs: io_intf * io
outputs: none
*/
void vioblk_close(struct io_intf * io) {
    
    //          Gain access to the device based on io
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);

    dev->io_intf.refcnt -= 1;  //Decrement device refence counter
    if(dev->io_intf.refcnt == 0){
    //          reset the used and avail virtqueues
    virtio_reset_virtq(dev->regs, VQ_NUM);

    plic_disable_irq(dev->irqno);

    //          disable the interrupt for this device
    intr_disable_irq(dev->irqno);

    //          set neccessary flags
    dev->opened = 0;
    }
}

/*
This function reads a number of bytes from disc as specified by bufsz. They are written into buf.
Reads larger than block size are handled by chaining block read commands.
inputs: io_intf, io, buf, bufsz
outputs: long (bytes written)
*/
long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz)
{
    //           FIXME your code here

    //      variable declarations
    uint64_t block_num;
    uint64_t block_position;
    unsigned long curr_read_size;
    unsigned long bytes_read = 0;
    int intr_status;

    //          Gain access to the device based on the io
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);

    //          Access the current block number and offset based on dev->pos and block size
    block_num = dev->pos / dev->blksz;
    block_position = dev->pos % dev->blksz;

    //          Acquire the Read/Write Lock before reading
    lock_acquire(&dev->RWLock);

    //          Loop to access multiple blocks if necessary
    while(bytes_read < bufsz){
        //          Set curr_read_size to correct 
        
        if((int)(dev->blksz - block_position) < (int)(bufsz - bytes_read))
            curr_read_size = dev->blksz - block_position;
        else
            curr_read_size = bufsz-bytes_read;

        //          Prepare our request header with type and sector
        dev->vq.req_header.type = VIRTIO_BLK_T_IN;
        dev->vq.req_header.sector = block_num;

        //          Ensure the data descriptor is writeable by the device through the flag
        dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;

        //          Prepare the available ring for the read
        dev->vq.avail.ring[dev->vq.avail.idx%1] = 0;
        __sync_synchronize();
        dev->vq.avail.idx++;
        __sync_synchronize();

        //          Notify the device that we have placed a buffer on avail
        virtio_notify_avail(dev->regs, VQ_NUM);

        //          Thread sleeps until the device puts buf onto used (also disable interrupts)
        intr_status = intr_disable();
        while(dev->vq.avail.idx != dev->vq.used.idx){
            condition_wait(&dev->vq.used_updated);
        }
        intr_restore(intr_status);

        //          Check status for successful read
        if(dev->vq.req_status != VIRTIO_BLK_S_OK) {
            lock_release(&dev->RWLock);
            return -EIO;
        }
        //          Data is now in block buffer cache, write to buf
        memcpy(buf + bytes_read, dev->blkbuf + block_position, curr_read_size);

        //          Update variables to move onto next iteration or finish
        block_num++;
        block_position = 0;
        bytes_read += curr_read_size;
    }
    
    //          Release the read/write lock after reading
    lock_release(&dev->RWLock);

    return bytes_read;
}

/*
This function writes the data specified in buf to the disc at the current position. Writes greater
than or less than blocksize are handled by reading before writing on the edge cases to preserve data.
inputs: io_intf io, buf, long n
outputs: long (bytes written)
*/
long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n)
{
    //          variable declarations
    uint64_t block_num, block_position;
    unsigned long curr_write_size;
    unsigned long bytes_written = 0;
    int intr_status;

    //          Access the device based on the io
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);

    char temp_buf[dev->blksz];

    //          Access the current block number and offset based on dev->pos and block size
    block_num = dev->pos / dev->blksz;
    block_position = dev->pos % dev->blksz;

    //          Acquire the Read/Write Lock before writing
    lock_acquire(&dev->RWLock);

    //          Loop to be able to write to multiple blocks if neccessary
    while(bytes_written < n){

        //          Set curr write size and copy bytes to write into block buffer cache
        if((int)(dev->blksz - block_position) < (int)(n - bytes_written))
            curr_write_size = dev->blksz - block_position;
        else
            curr_write_size = n - bytes_written;

        //          Check if we need to begin by writing an incomplete block, 
        //              if so read the beginning to maintain it
        int error;
        if(block_position != 0 || curr_write_size < dev->blksz){
            uint64_t read_pos = block_num * dev->blksz;
            uint64_t write_pos = dev->pos;
            dev->pos = read_pos;
            error = vioblk_read(io, temp_buf, dev->blksz);
            dev->pos = write_pos;
            lock_acquire(&dev->RWLock);
            if(error < 0) {
                lock_release(&dev->RWLock);
                return error;     
            }
            memcpy(temp_buf + block_position, buf, curr_write_size);
            memcpy(dev->blkbuf, temp_buf, dev->blksz);
        }
        else
            memcpy(dev->blkbuf, buf + bytes_written, curr_write_size);


        //          Prepare our request header with the type and sector
        dev->vq.req_header.type = VIRTIO_BLK_T_OUT;
        dev->vq.req_header.sector = block_num;

        //          Ensure the data descriptor is not writeable by the device,
        dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT;

        //          Prepare the available ring for the write
        dev->vq.avail.ring[dev->vq.avail.idx % 1] = 0;
        dev->vq.avail.idx++;
        __sync_synchronize();

        //          Notify the device that we have placed a buffer on avail
        virtio_notify_avail(dev->regs, VQ_NUM);   

        //          Thread sleeps until the device puts buf onto used
        intr_status = intr_disable();
        while(dev->vq.avail.idx != dev->vq.used.idx){
            condition_wait(&dev->vq.used_updated);
        }
        intr_restore(intr_status);

        //          Check status for successful read
        if(dev->vq.req_status != VIRTIO_BLK_S_OK) {
            lock_release(&dev->RWLock);
            return -EIO;
        }
        //          Update variables to move onto next iteration or finish
        block_num++;
        block_position = 0;
        bytes_written += curr_write_size;
    }

    //          Release the read/write lock after writing
    lock_release(&dev->RWLock);

    return bytes_written;
}

int vioblk_ioctl(struct io_intf * restrict io, int cmd, void * restrict arg) {
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);
    
    trace("%s(cmd=%d,arg=%p)", __func__, cmd, arg);
    lock_acquire(&dev->RWLock);
    int result;
    switch (cmd) {
    case IOCTL_GETLEN:
        result = vioblk_getlen(dev, arg);
        break;
    case IOCTL_GETPOS:
        result = vioblk_getpos(dev, arg);
        break;
    case IOCTL_SETPOS:
        result = vioblk_setpos(dev, arg);
        break;
    case IOCTL_GETBLKSZ:
        result =  vioblk_getblksz(dev, arg);
        break;
    default:
        result = -ENOTSUP;
    }
    lock_release(&dev->RWLock);
    return result;
}

/*
This function is the ISR for our device, which most of the time handles when the device adds something
to the used buffer. Config change interrupts are also handled. The aux is the device to be able 
to signal the condition.
inputs: irqno, aux
outputs: none
*/
void vioblk_isr(int irqno, void * aux) {

    //          Variable Declarations
    #define USED_BUF_NOTIF 1
    #define CONFIG_CHANGE_NOTIF 2
    virtio_featset_t enabled_features, wanted_features, needed_features;
    int result;

    //          Gain access to the device, aux is the device pointer as specified in virtblk_attach
    struct vioblk_device * const dev = (struct vioblk_device *)(aux);
    uint32_t status = dev->regs->interrupt_status;

    //          ISR signaled from used buffer filling, signal used_updated
    if(status == USED_BUF_NOTIF ){
        condition_broadcast(&dev->vq.used_updated);
    }

    //          ISR signaled from configuration change, re-negotiate features
    if(status == CONFIG_CHANGE_NOTIF){
        virtio_featset_init(needed_features);
        virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
        virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
        virtio_featset_init(wanted_features);
        virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
        virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
        result = virtio_negotiate_features(dev->regs,
        enabled_features, wanted_features, needed_features);

        if (result != 0) {
            kprintf("%p: virtio feature negotiation failed\n", dev->regs);
            return;
        }
    }

    //          Signal to the device that we handled the interrupt by writing to intr_ack
    dev->regs->interrupt_ack = status;
}

/*
Put the device size in bytes into lenptr
inputs: dev, lenptr
outputs: int (success/fail code)
*/
int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr) {
    //          Check the device exists, return error if not
    if(dev == NULL)
        return -EINVAL;

    //          Set the lenptr value to the current device size, return 0 for success
    *lenptr = dev->size;
    return 0;
}

/*
Put the current disc position into posptr
inputs: dev, posptr
outputs: int (success/fail code)
*/
int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr) {

    //          Check the device exists, return error if not
    if(dev == NULL)
        return -EINVAL;

    //          Set the lenptr value to the current device size, return 0 for success
    *posptr = dev->pos;
    return 0;
}

/*
Set the current disc position to the data in posptr
inputs: dev, lenptr
outputs: int (success/fail code)
*/
int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr) {
    
    //          Check that the device and position we are trying to set to are not NULL and within device size bounds
    if(dev == NULL || posptr == NULL)
        return -dev->size;

    //          set the new position, return 0 on success
    dev->pos = *posptr;
    return 0;
}

/*
Put the block size in bytes into blkszptr
inputs: dev, lenptr
outputs: int (success/fail code)
*/
int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr)
{
 
    //          Check the device exists, return error if not
    if(dev == NULL)
        return -EINVAL;

    //          Set the lenptr value to the current device size, return 0 for success
    *blkszptr = dev->blksz;
    return 0;
}
