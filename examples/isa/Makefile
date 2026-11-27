XLEN ?= 64
RISCV_PREFIX ?= riscv64-unknown-elf-
OUT_DIR ?= /tmp/simrv-isa-rv$(XLEN)

ifeq ($(XLEN),64)
ARCH := rv64gc
ABI := lp64d
else ifeq ($(XLEN),32)
ARCH := rv32gc
ABI := ilp32d
else
$(error XLEN must be 32 or 64)
endif

CC := $(RISCV_PREFIX)gcc
SOURCES := $(wildcard *.S)
ELFS := $(patsubst %.S,$(OUT_DIR)/%.elf,$(SOURCES))

.PHONY: all list

all: $(ELFS)

list:
	@printf '%s\n' $(ELFS)

$(OUT_DIR):
	mkdir -p $@

$(OUT_DIR)/%.elf: %.S linker.ld | $(OUT_DIR)
	$(CC) -march=$(ARCH) -mabi=$(ABI) -nostdlib -static -T linker.ld -o $@ $<
