#include "syscall.h"
#include "string.h"
#include "stdint.h"

#define FILE_FID 1
#define IOCTL_SETPOS 4

void main(void) { 
    int result;
    char read_buf[300];
    char * string = "First parent write!\n";
    _msgout("Beginning locking test:\n");

    // Open ser1 device as fd=0
    result = _devopen(0, "ser", 1);
    if (result < 0)
        return;

    //open the test_file as fd=1
    result = _fsopen(FILE_FID, "test");
    if (result < 0) {
        _msgout("_fsopen failed");
        _exit();
    }

    result = _fork();
    if(result){ //parent process
        //  Write four times to the file
        string = "First parent write! ";
        _write(FILE_FID, string, strlen(string));
        string = "Second parent write! ";
        _write(FILE_FID, string, strlen(string));
        string = "Third parent write! ";
        _write(FILE_FID, string, strlen(string));
        string = "Fourth parent write! ";
        _write(FILE_FID, string, strlen(string));

        _wait(result);  //wait for child to exit

        //  Set file position to 0
        uint64_t pos = 0;
        _ioctl(FILE_FID, IOCTL_SETPOS, &pos);

        //  Read the entire file and print to console
        _read(FILE_FID, read_buf, 300);
        _msgout("Finished reading:");
        _msgout(read_buf);

        //Close the file and exit
        _close(FILE_FID);
        _exit();

    } else { //child process
        // Write four times to the file, then exit the child process
        char * string = "First child write! ";
        //_ioctl(1, 3, &pos);
        _write(FILE_FID, string, strlen(string));
        string = "Second child write! ";
        _write(FILE_FID, string, strlen(string));
        string = "Third child write! ";
        _write(FILE_FID, string, strlen(string));
        string = "Fourth child write! ";
        _write(FILE_FID, string, strlen(string));
        _exit();
    }
}