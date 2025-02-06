#include "syscall.h"
#include "string.h"
#include "stdint.h"

#define FILE_FID 1

void main(void) {   //Test to show 31 fork capacity
    int result;
    _msgout("Beginning fork overflow test:\n");

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

    for(int i=0; i<5; i++){ //Fork 32 times (2^5)
        _fork();
    }
}