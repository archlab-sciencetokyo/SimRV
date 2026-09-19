"""Shared benchmark, toolchain, and trace helpers for RTL parity evaluators."""

import re
import shutil


BENCHMARKS = {
    "arithmetic_loop": """
int main(void) {
    int a = 1;
    int b = 2;
    for (int i = 0; i < 100; i++) {
        a = (a * 3) + b;
        b = a ^ (b << 1);
    }
    return a + b;
}
""",
    "raw_hazard_chain": """
int main(void) {
    register int v = 42;
    for (int i = 0; i < 100; i++) {
        asm volatile (
            "add %[v], %[v], %[i]\\n"
            "sub %[v], %[v], %[i]\\n"
            "add %[v], %[v], %[i]\\n"
            "sub %[v], %[v], %[i]\\n"
            : [v] "+r" (v) : [i] "r" (i)
        );
    }
    return v;
}
""",
    "branch_dense": """
int main(void) {
    int s = 0;
    for (int i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            s += i;
        } else {
            s -= i;
        }
    }
    return s;
}
""",
    "mul_div_suite": """
int main(void) {
    int a = 12345;
    int b = 67;
    int s = 0;
    for (int i = 1; i <= 20; i++) {
        s += (a * i) / (b + i);
    }
    return s;
}
""",
    "load_store_array": """
int arr[64];
int main(void) {
    for (int i = 0; i < 64; i++) {
        arr[i] = i * 3 + 1;
    }
    int sum = 0;
    for (int i = 0; i < 64; i++) {
        sum += arr[i];
    }
    return sum;
}
""",
}


def get_toolchain():
    """Return a usable RV32 GCC, objcopy, and objdump toolchain tuple."""
    candidates = (
        "riscv32-linux-gnu-gcc",
        "riscv32-unknown-elf-gcc",
        "riscv64-linux-gnu-gcc",
        "riscv64-unknown-elf-gcc",
    )
    for gcc in candidates:
        if shutil.which(gcc):
            prefix = gcc[:-3]
            return gcc, f"{prefix}objcopy", f"{prefix}objdump"
    raise RuntimeError("No suitable RISC-V toolchain found in PATH")


def extract_number(pattern, text):
    """Extract a comma-separated decimal integer or return None."""
    match = re.search(pattern, text)
    return int(match.group(1).replace(",", "")) if match else None


def parse_retirement_trace(text):
    """Parse the stable CYCLETRACE interchange format."""
    pattern = re.compile(
        r"CYCLETRACE cycle=(\d+) inst=(\d+) pc=([0-9a-fA-F]+) ir=([0-9a-fA-F]+)"
    )
    return [
        tuple(int(value, 16 if index >= 2 else 10)
              for index, value in enumerate(match.groups()))
        for match in pattern.finditer(text)
    ]


def compare_retirement_traces(rtl_out, simrv_out):
    """Compare architecture and inter-retirement timing for two trace streams."""
    rtl = parse_retirement_trace(rtl_out)
    simrv = parse_retirement_trace(simrv_out)
    first_architectural_divergence = None
    first_timing_divergence = None
    for index, (rtl_event, simrv_event) in enumerate(zip(rtl, simrv)):
        if rtl_event[2:] != simrv_event[2:]:
            first_architectural_divergence = index
            break
        rtl_step = rtl_event[0] - (rtl[index - 1][0] if index else 0)
        simrv_step = simrv_event[0] - (simrv[index - 1][0] if index else 0)
        if first_timing_divergence is None and rtl_step != simrv_step:
            first_timing_divergence = {
                "index": index,
                "pc": f"0x{rtl_event[2]:08x}",
                "rtl_step_cycles": rtl_step,
                "simrv_step_cycles": simrv_step,
            }
    return {
        "rtl_events": len(rtl),
        "simrv_events": len(simrv),
        "event_count_delta": len(simrv) - len(rtl),
        "first_unpaired_event": min(len(rtl), len(simrv)) if len(rtl) != len(simrv) else None,
        "first_architectural_divergence": first_architectural_divergence,
        "first_timing_divergence": first_timing_divergence,
    }
