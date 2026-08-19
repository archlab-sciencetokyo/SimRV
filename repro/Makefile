CC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy
OBJDUMP = riscv64-unknown-elf-objdump

# Default to beta13, but allow SIMRV=... for comparison.
SIMRV ?= ./SimRV-rv64-linux-x86_64-v2.0.0-beta.13.bin
OUT_DIR ?= ./out_repro

CPPFLAGS ?= -I. -Ibaremetal
CFLAGS ?= -O2 -march=rv64gcv -mabi=lp64d -mcmodel=medany -ffreestanding -fno-builtin -DRVV_BAREMETAL -Wall -Wextra
LDFLAGS ?= -nostdlib -T baremetal/linker.ld -Wl,--gc-sections
LDLIBS ?= -lgcc

# Reuse the bare-metal startup, UART, and finisher code.
RUNTIME_SRCS := baremetal/start.S baremetal/runtime.c
RUNTIME_DEPS := $(RUNTIME_SRCS) baremetal/runtime.h baremetal/linker.ld

# Keep one source file per suspect instruction.
TESTS := simrv_beta13_vlse32 simrv_beta13_vluxei32 simrv_beta13_vfmacc simrv_h_mmu
ELFS := $(addprefix $(OUT_DIR)/,$(addsuffix .elf,$(TESTS)))
BINS := $(ELFS:.elf=.bin)
DUMPS := $(ELFS:.elf=.dump)

# Function dumps show only the RVV function under test.
FUNC_DUMPS := \
  $(OUT_DIR)/simrv_beta13_vlse32.func.dump \
  $(OUT_DIR)/simrv_beta13_vluxei32.func.dump \
  $(OUT_DIR)/simrv_beta13_vfmacc.func.dump

.PHONY: all dump dump-full dump-func run run-vlse32 run-vluxei32 run-vfmacc clean
.SECONDARY: $(ELFS)

all: $(BINS)

$(OUT_DIR):
	mkdir -p $@

$(OUT_DIR)/%.elf: %.c $(RUNTIME_DEPS) | $(OUT_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(RUNTIME_SRCS) $(LDFLAGS) -o $@ $(LDLIBS)

$(OUT_DIR)/%.bin: $(OUT_DIR)/%.elf
	$(OBJCOPY) -O binary $< $@

$(OUT_DIR)/%.dump: $(OUT_DIR)/%.elf
	$(OBJDUMP) -d $< > $@

$(OUT_DIR)/simrv_beta13_vlse32.func.dump: $(OUT_DIR)/simrv_beta13_vlse32.elf
	$(OBJDUMP) -d --disassemble=gather_vlse32 $< > $@

$(OUT_DIR)/simrv_beta13_vluxei32.func.dump: $(OUT_DIR)/simrv_beta13_vluxei32.elf
	$(OBJDUMP) -d --disassemble=lookup_vluxei32 $< > $@

$(OUT_DIR)/simrv_beta13_vfmacc.func.dump: $(OUT_DIR)/simrv_beta13_vfmacc.elf
	$(OBJDUMP) -d --disassemble=saxpy_vfmacc $< > $@

dump: dump-full dump-func

dump-full: $(DUMPS)
	@for f in $(DUMPS); do echo "== $$f =="; done

dump-func: $(FUNC_DUMPS)
	@for f in $(FUNC_DUMPS); do echo "== $$f =="; done

run: $(BINS)
	set -e; for f in $(BINS); do \
		echo "== $$f =="; \
		$(SIMRV) -b -m $$f; \
	done

run-vlse32: $(OUT_DIR)/simrv_beta13_vlse32.bin
	$(SIMRV) -b -m $<

run-vluxei32: $(OUT_DIR)/simrv_beta13_vluxei32.bin
	$(SIMRV) -b -m $<

run-vfmacc: $(OUT_DIR)/simrv_beta13_vfmacc.bin
	$(SIMRV) -b -m $<

clean:
	rm -rf $(OUT_DIR)
