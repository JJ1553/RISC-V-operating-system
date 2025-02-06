# Based on xv6-riscv Makefile; see LICENSE.

TOOLPREFIX=riscv64-unknown-elf-
QEMU=qemu-system-riscv64

CC=$(TOOLPREFIX)gcc
AS=$(TOOLPREFIX)as
LD=$(TOOLPREFIX)ld
OBJCOPY=$(TOOLPREFIX)objcopy
OBJDUMP=$(TOOLPREFIX)objdump

CORE_OBJS = \
	start.o \
	halt.o \
	string.o \
	trapasm.o \
	intr.o \
	plic.o \
	timer.o \
	thread.o \
	thrasm.o \
	ezheap.o \
	io.o \
	device.o \
	uart.o \
	virtio.o \
	vioblk.o \
	console.o \
	excp.o \
	memory.o \
	elf.o \
	process.o \
	kfs.o \
	syscall.o
	# Add more object files here

CFLAGS = -Wall -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64g -mabi=lp64d
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -I. # -DDEBUG -DTRACE

QEMUOPTS = -global virtio-mmio.force-legacy=false
QEMUOPTS += -machine virt -bios none -kernel $< -m 8M -nographic
QEMUOPTS += -serial mon:stdio
QEMUOPTS += -drive file=kfs.raw,id=blk0,if=none,format=raw
QEMUOPTS += -device virtio-blk-device,drive=blk0
QEMUOPTS += -serial pty
QEMUOPTS += -monitor pty

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

# compile files
all: kernel.elf

kernel.elf: $(CORE_OBJS) main.o companion.o
	$(LD) -T kernel.ld -o $@ $^

test.elf: $(CORE_OBJS) main_test_elf.o companion.o
	$(LD) -T kernel.ld -o $@ $^

test.fs: $(CORE_OBJS) main_test_fs.o companion.o
	$(LD) -T kernel.ld -o $@ $^

test.vioblk: $(CORE_OBJS) main_test_virtio.o companion.o
	$(LD) -T kernel.ld -o $@ $^

test.memory: $(CORE_OBJS) main_test_memory.o companion.o
	$(LD) -T kernel.ld -o $@ $^

test.process: $(CORE_OBJS) main_test_process.o companion.o
	$(LD) -T kernel.ld -o $@ $^

test.main: $(CORE_OBJS) main.o companion.o
	$(LD) -T kernel.ld -o $@ $^

# run files

run-kernel: kernel.elf
	$(QEMU) $(QEMUOPTS)	

run-test-fs: test.fs 
	$(QEMU) $(QEMUOPTS)

run-test-vioblk: test.vioblk
	$(QEMU) $(QEMUOPTS)

run-test-elf: test.elf
	$(QEMU) $(QEMUOPTS)

run-test-memory: test.memory
	$(QEMU) $(QEMUOPTS)

run-test-process: test.process
	$(QEMU) $(QEMUOPTS)

run-test-main: test.main
	$(QEMU) $(QEMUOPTS)

# debug files

debug-kernel: kernel.elf
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

debug-test-memory: test.memory
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

debfug-test-process: test.process
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)	

debug-test-main: test.main
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

test.syscalls: $(CORE_OBJS) main_test_syscalls.o companion.o
	$(LD) -T kernel.ld -o $@ $^

run-test-syscalls: test.syscalls
	$(QEMU) $(QEMUOPTS)

debug-test-syscalls: test.syscalls
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)
	
clean:
	if [ -f companion.o ]; then cp companion.o companion.o.save; fi
	rm -rf *.o *.elf *.asm
	if [ -f companion.o.save ]; then mv companion.o.save companion.o; fi

# This will load the trek file into your kernel memory, via kernel.ld
# `mkcomp.sh`, as well as the documentation, contain discussion
companion.o:
	if [ -f ../user/trek ]; then sh ./mkcomp.sh ../user/trek; fi
	if ! [ -f ../user/trek ]; then sh ./mkcomp.sh; fi
