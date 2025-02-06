#include "syscall.h"
#include "string.h"

void main(void){

    //On-demand paging test: store a string into unallocated memory, show paging
    char * string = (char *)0xC0000000;
    string = "I love ECE 391!";
    _msgout(string);
    

    //User mode error test: attempt to read from kernel space
    //char * kern_string = (char *)0x80000000;
    //char character = *kern_string;
    //_msgout(&character);
}