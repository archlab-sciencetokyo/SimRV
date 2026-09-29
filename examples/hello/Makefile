RISCV_PREFIX ?= riscv64-unknown-elf-
XLEN ?= 64
OUT ?= build-rv$(XLEN)

ifeq ($(XLEN),32)
  MABI := ilp32
else ifeq ($(XLEN),64)
  MABI := lp64
else
  $(error XLEN must be 32 or 64)
endif

CC := $(RISCV_PREFIX)gcc
OBJCOPY := $(RISCV_PREFIX)objcopy
CFLAGS := -march=rv$(XLEN)imac -mabi=$(MABI) -mno-relax -ffreestanding -nostdlib -nostartfiles -O2
LDFLAGS := -T linker.ld -nostdlib -Wl,--gc-sections

.PHONY: all clean

all: $(OUT)/hello.elf $(OUT)/hello.bin

$(OUT):
	mkdir -p $@

$(OUT)/hello.elf: hello.S linker.ld | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) hello.S -o $@

$(OUT)/hello.bin: $(OUT)/hello.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf build-rv32 build-rv64
