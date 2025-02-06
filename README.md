# RISC-V Operating System

This project is a Unix-like operating system developed as part of ECE391: Computer Systems Engineering. 
It's a functional operating system that implements core Unix concepts including filesystem management, process management, virtual memory, and system calls.

## Project Structure
The project is divided into three major checkpoints:

### Checkpoint 1: Filesystem and Drivers
- **VirtIO Block Device Driver**: Manages disk I/O operations
- **Filesystem Implementation**: Supports file operations with custom filesystem format
- **Program Loading**: ELF file loading capability
- **Basic I/O Operations**: Terminal and device I/O handling

### Checkpoint 2: Virtual Memory and Process Management
- **Virtual Memory Implementation**: Sv39 paging system
- **Process Abstraction**: Basic process management
- **System Calls**: Core syscall implementation
- **User/Supervisor Mode**: Privilege level management

### Checkpoint 3: Advanced Features
- **Fork Implementation**: Process creation and management
- **Reference Counting**: Resource management
- **Preemptive Multitasking**: Task scheduling
- **Sleep/Wait Operations**: Process synchronization

## Prerequisites
- RISC-V toolchain
- QEMU RISC-V emulator
- Make build system

## Core Components

### Filesystem
- 4KB block size
- Support for up to 63 files
- Directory entry system
- File operations (open, read, write, close)

### Virtual Memory
- Sv39 paging system
- User/kernel space separation
- Page fault handling
- Memory mapping operations

### Process Management
- Process creation and termination
- System call interface
- Resource management
- Process scheduling

### Device Drivers
- VirtIO block device support
- UART driver
- Terminal I/O
- Interrupt handling

## System Calls
Core syscalls implemented:
- `exit`: Process termination
- `exec`: Program execution
- `fork`: Process creation
- `read/write`: File operations
- `open/close`: File descriptor management
- `wait`: Process synchronization
- `sleep`: Process scheduling

## Debugging
- GDB support for both kernel and user programs
- QEMU debugging interface
- Console output for debugging
- Built-in test framework

# Academic Collaboration & Attribution Notice

This repository contains work completed as part of ECE 391 (Computer Systems Engineering) at the University of Illinois at Urbana-Champaign in Fall 2024. 

## Important Disclaimers

1. **Course Infrastructure**: Key infrastructure code and project framework were created and provided by UIUC course staff as a foundation to start on, I do not take any credit for their work

2. **Collaborative Work**: The implementation code represents the combined efforts of a 3 person team. No single person claims sole credit for the complete implementation.

3. **Academic Integrity Notice**: 
   - This code is shared for educational and portfolio purposes only
   - If you are currently enrolled in ECE 391 or plan to take it:
     - Do not copy, reference, or use any part of this code
     - Doing so would constitute a violation of academic integrity policies
     - Develop your own implementation based on course instruction

4. **Usage Rights**: While this code is publicly available for viewing, it incorporates intellectual property belonging to UIUC ECE department and was developed under their academic framework. Any usage must respect university policies and academic integrity guidelines.
