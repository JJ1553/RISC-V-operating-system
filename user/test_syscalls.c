#include "syscall.h"
#include "string.h"
#include <stdlib.h>
#include <math.h>

#define IOCTL_GETLEN        1
#define IOCTL_SETLEN        2
#define IOCTL_GETPOS        3
#define IOCTL_SETPOS        4
#define IOCTL_FLUSH         5
#define IOCTL_GETBLKSZ      6

#define PTE_V (1 << 0)
#define PTE_R (1 << 1)  // x2
#define PTE_W (1 << 2)  // x4
#define PTE_X (1 << 3)  // x8
#define PTE_U (1 << 4)
#define PTE_G (1 << 5)
#define PTE_A (1 << 6)
#define PTE_D (1 << 7)

#define LCG_A 1664525
#define LCG_C 1013904223
#define LCG_M 4294967296 // 2^32

unsigned int seed = 12345;

unsigned int lcg_rand(void) {
    seed = (LCG_A * seed + LCG_C) % LCG_M;
    return seed;
}
long my_strtol(const char *nptr, char **endptr, int base);

void initialize_seed(int fd) {
    char buf[128];
    const char *prompt = "Enter a number to initialize the random seed (0 to 4294967295): ";
    _write(fd, prompt, strlen(prompt));

    int i = 0;
    while (1) {
        char c;
        if (_read(fd, &c, 1) <= 0) {
            _write(fd, "Error reading input\n", 20);
            continue;
        }
        _write(fd, &c, 1); // Echo the character back to the user
        if (c == '\r' || c == '\n') {
            buf[i] = '\0';
            break;
        } else {
            buf[i++] = c;
        }
    }

    long input_seed = my_strtol(buf, NULL, 10);

    // Ensure the seed is within the valid range for unsigned int
    if (input_seed < 0 || input_seed > 4294967295) {
        const char *invalid_seed = "Invalid seed value. Using default seed.\n";
        _write(fd, invalid_seed, strlen(invalid_seed));
        seed = 12345;
    } else {
        seed = (unsigned int)input_seed;
    }
}

long my_strtol(const char *nptr, char **endptr, int base) {
    long result = 0;
    int sign = 1;

    // Skip whitespace
    while (*nptr == ' ' || *nptr == '\t') {
        nptr++;
    }

    // Handle optional sign
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    // Convert digits
    while ((*nptr >= '0' && *nptr <= '9') || 
           (base == 16 && (*nptr >= 'a' && *nptr <= 'f')) || 
           (base == 16 && (*nptr >= 'A' && *nptr <= 'F'))) {
        int digit;
        if (*nptr >= '0' && *nptr <= '9') {
            digit = *nptr - '0';
        } else if (*nptr >= 'a' && *nptr <= 'f') {
            digit = *nptr - 'a' + 10;
        } else {
            digit = *nptr - 'A' + 10;
        }
        result = result * base + digit;
        nptr++;
    }

    if (endptr) {
        *endptr = (char *)nptr;
    }

    return result * sign;
}

void cat(const char *filename) {
    int fd = _fsopen(1, filename);
    if (fd < 0) {
        _msgout("Failed to open file\n");
        _exit();
    }

    char buf[128];
    long n;
    while ((n = _read(fd, buf, sizeof(buf))) > 0) {
        _write(1, buf, n);
    }

    _close(fd);
}

void number_guessing_game(int fd) {
    initialize_seed(fd);
    const int target = (lcg_rand() % 100) + 1; // Target number to guess

    int guess;
    char buf[128];
    char *endptr;
    const char *higher = "Higher\r\n";
    const char *lower = "Lower\r\n";
    const char *correct = "Congrats! You guessed the right number!\r\n";
    const char *intro = "Welcome to the number guessing game!\r\n";
    const char *instructions = "Guess the number (1-100):\r\n";
    const char *invalid = "Invalid input. Please enter a number.\r\n";
    const char *newline = "\r\n";
    _write(fd, intro, strlen(intro));
    _write(fd, instructions, strlen(instructions));

    while (1) {
        int i = 0;
        for (;;) {
            char c;
            if (_read(fd, &c, 1) <= 0) {
                _write(fd, invalid, strlen(invalid));
                continue;
            }
            _write(fd, &c, 1);
            if (c == '\r' || c == '\n') {
                buf[i] = '\0';
                break;
            } else {
                buf[i++] = c;
            }
        }
        guess = my_strtol(buf, &endptr, 10);

        if (*endptr != '\0') {
            _write(fd, invalid, strlen(invalid));
            continue;
        }

        if (guess < target) {
            _write(fd, buf, i);
            _write(fd, newline, strlen(newline));
            _write(fd, higher, strlen(higher));
        } else if (guess > target) {
            _write(fd, buf, i);
            _write(fd, newline, strlen(newline));
            _write(fd, lower, strlen(lower));
        } else {
            _msgout(correct);
            break;
        }
    }
}

void ls(void) {
    // Assuming the boot block is at a known location and contains the file list
    // This is a simplified example; actual implementation may vary
    _msgout("Listing files:\n");

    // Open the boot block (assuming it's a file named "boot_block")
    int fd = _fsopen(0, "boot_block");
    if (fd < 0) {
        _msgout("Failed to open boot block\n");
        _exit();
    }

    char buf[128];
    long n;
    while ((n = _read(fd, buf, sizeof(buf))) > 0) {
        _write(1, buf, n);
    }

    _close(fd);
}

int main(void) {
    _msgout("Testing syscalls...");

    // Test _write and _read syscalls
    const char *msg = "Hello, world!";
    char buf[20];
    int fd = _devopen(0, "ser", 1);
    if (fd < 0) {
        _msgout("Failed to open device\n");
        _exit();
    }
    _write(fd, msg, strlen(msg));
    _read(fd, buf, sizeof(buf));
    _msgout(buf);
    // _close(fd);

    // Additional tests
    // cat("init0");
    _msgout("Testing read, write, and msg_out");
    number_guessing_game(fd);
    _msgout("\n");

    int fd2 = _fsopen(1, "hello");

//IOCTL SETPOS TESTING ****************************************************
    _msgout("Testing ioctl setpos.");
    int pos = 5;
    _ioctl(fd2, IOCTL_SETPOS, &pos);
    _msgout("Setting position to 5");
// ****************************************************


//IOCTL getpos TESTING ****************************************************
    _msgout("Testing ioctl getpos.");
    _ioctl(fd2, IOCTL_GETPOS, &pos);
    char pos_buf[20];
    snprintf(pos_buf, sizeof(pos_buf), "%d", pos);
    _msgout("Current position: ");
    _msgout(pos_buf);
    _msgout("\n");
// ****************************************************


//IOCTL get BLK size TESTING ****************************************************
    _msgout("Testing ioctl getblksz.");
    int blksize;
    char blksize_buf[20];
    _ioctl(fd2, IOCTL_GETBLKSZ, &blksize);
    snprintf(blksize_buf, sizeof(blksize_buf), "%d", blksize);
    _msgout("Get block size: ");
    _msgout(blksize_buf);
    _msgout("\n");
// ****************************************************


//IOCTL GET LENTH TESTING ****************************************************
    _msgout("Testing ioctl getlen.");
    int len;
    char len_buf[20];
    _ioctl(fd2, IOCTL_GETLEN, &len);
    snprintf(len_buf, sizeof(len_buf), "%d", len);
    _msgout("Current Len: ");
    _msgout(len_buf);
    _msgout("\n");
// ****************************************************


//close TESTING ****************************************************
    _msgout("Testing close.");
    _close(fd2);
    _msgout("\n");
// ****************************************************



//Extra credit TESTING ****************************************************
    _msgout("Testing Extra Credit Functions.");
    _msgout("Testing memory_validate_vptr_len.");
    
    char *invalid_ptr = (char *)0xDEAFDADA;
    char sys_buf[100];
    int result = _read(fd, invalid_ptr, sizeof(sys_buf));

    if (result < 0) {
        _msgout("read correctly failed for invalid buffer.\n");
    } else {
        _msgout("read incorrectly succeeded for invalid buffer.\n");
    }
     _exit();
// ****************************************************
}