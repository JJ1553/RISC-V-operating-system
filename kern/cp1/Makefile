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
	io.o \
	device.o \
	virtio.o \
	vioblk.o \
	console.o \
	plic.o \
	kfs.o \
	thread.o \
	thrasm.o \
	timer.o \
	uart.o \
	ezheap.o \
	elf.o

	# Add the names of new objects here

CFLAGS = -Wall -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64g -mabi=lp64d
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -I. #-DDEBUG -DTRACE

QEMUOPTS = -global virtio-mmio.force-legacy=false
QEMUOPTS += -machine virt -bios none -kernel $< -m 8M -nographic
QEMUOPTS += -serial mon:stdio
QEMUOPTS += -drive file=kfs.raw,id=blk0,if=none,format=raw
QEMUOPTS += -device virtio-blk-device,drive=blk0
QEMUOPTS += -serial pty

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

all: shell.elf

shell.elf: $(CORE_OBJS) main_shell.o companion.o
	$(LD) -T kernel.ld -o $@ $^

run-shell: shell.elf
	$(QEMU) $(QEMUOPTS)

debug-shell: shell.elf
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

# You might want to add more here
test.elf: $(CORE_OBJS) main_test_elf.o companion.o
	$(LD) -T kernel.ld -o $@ $^

test.fs: $(CORE_OBJS) main_test_fs.o companion.o
	$(LD) -T kernel.ld -o $@ $^

test.vioblk: $(CORE_OBJS) main_test_virtio.o companion.o
	$(LD) -T kernel.ld -o $@ $^

#run to test filsystem 
run-test-fs: test.fs 
	$(QEMU) $(QEMUOPTS)

run-test-vioblk: test.vioblk
	$(QEMU) $(QEMUOPTS)

#run to test elf
run-test-elf: test.elf
	$(QEMU) $(QEMUOPTS)

debug-test: test.elf
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

clean:
	if [ -f companion.o ]; then cp companion.o companion.o.save; fi
	rm -rf *.o *.elf *.asm
	if [ -f companion.o.save ]; then mv companion.o.save companion.o; fi

companion.o:
	if [ -f ../user/trek ]; then sh ./mkcomp.sh ../user/trek; fi
	if ! [ -f ../user/trek ]; then sh ./mkcomp.sh; fi
