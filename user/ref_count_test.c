#include "syscall.h"
#include "string.h"
#include "stdint.h"

#define FILE_FID 1
#define IOCTL_SETPOS 4

void main(void) {
    
    int result;
    const char * const output = "Writing this string to the file! ";
    const char * const output2 = "Very Cool";
    char read_buf[strlen(output) + strlen(output2)];
    _msgout("Beginning ref count test:\n");

    // Open ser1 device as fd=0
    result = _devopen(0, "ser", 1);
    if (result < 0)
        return;

    //open the test as fd=1
    result = _fsopen(FILE_FID, "test");
    if (result < 0) {
        _msgout("_fsopen failed");
        _exit();
    }

    result = _fork();
    if(result){ //parent process
        _close(FILE_FID); //Close the test file
        _msgout("File closed by parent, waiting for child");
        _wait(result);  //Wait for the child thread to exit
        _msgout("Child exited, now exiting parent");
        _exit(); // Exit the parent process

    } else { //child
        //Write both strings to file
        _write(FILE_FID, output, strlen(output));
        _write(FILE_FID, output2, strlen(output2));
        _msgout("Finished writing\n");

        //Set file position to 0
        uint64_t position = 0;
        _ioctl(FILE_FID, IOCTL_SETPOS, &position); //4 = IOCTL_SETPOS

        //  Read what we wrote from the file and print it
        _read(FILE_FID, read_buf, strlen(output) + strlen(output2));
        _msgout("Finished reading:");
        _msgout(read_buf);

        //  Close the file
        _close(FILE_FID);
        _exit();
    }
}