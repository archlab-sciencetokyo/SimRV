#!/usr/bin/env python3
"""
SimRV CPU Model Scaffolding and Preset Wizard.
Interactive tool to generate, customize, and validate human-editable .cfg microarchitecture profiles.
"""

import argparse
import os
import sys
from pathlib import Path

TEMPLATES = {
    "tiny": {
        "description": "Minimal 3-stage microcontroller core",
        "xlen": 0,
        "misa": "imac",
        "pipeline_type": "three-stage",
        "enable_forwarding": False,
        "mul_latency": 3,
        "div_latency": 18,
        "fp_alu_latency": 4,
        "fp_div_latency": 16,
        "branch_mispredict_penalty": 2,
        "cycle_counter_start_delay": 0,
        "csr_flush_penalty": 1,
        "fence_flush_penalty": 1,
        "bpred_type": "static",
        "btb_entries": 128,
        "bht_entries": 256,
        "ras_entries": 4,
        "ghr_bits": 0,
        "pc_shift": 1,
        "enable_btb": False,
        "enable_ras": False,
        "untagged_btb": False,
        "registered_btb_read": False,
        "bht_initial_state": 0,
        "icache_capacity": 2048,
        "icache_ways": 1,
        "icache_line": 32,
        "icache_hit": 1,
        "icache_miss": 12,
        "dcache_capacity": 2048,
        "dcache_ways": 1,
        "dcache_line": 32,
        "dcache_hit": 1,
        "dcache_miss": 12,
        "interconnect_req": 1,
        "interconnect_resp": 1,
    },
    "balanced": {
        "description": "Default balanced 5-stage general-purpose core",
        "xlen": 0,
        "misa": "gcbv",
        "pipeline_type": "five-stage",
        "enable_forwarding": True,
        "mul_latency": 3,
        "div_latency": 18,
        "fp_alu_latency": 4,
        "fp_div_latency": 16,
        "branch_mispredict_penalty": 3,
        "cycle_counter_start_delay": 0,
        "csr_flush_penalty": 3,
        "fence_flush_penalty": 4,
        "bpred_type": "bimodal",
        "btb_entries": 256,
        "bht_entries": 1024,
        "ras_entries": 16,
        "ghr_bits": 10,
        "pc_shift": 1,
        "enable_btb": True,
        "enable_ras": True,
        "untagged_btb": False,
        "registered_btb_read": False,
        "bht_initial_state": 0,
        "icache_capacity": 4096,
        "icache_ways": 2,
        "icache_line": 32,
        "icache_hit": 1,
        "icache_miss": 10,
        "dcache_capacity": 4096,
        "dcache_ways": 2,
        "dcache_line": 32,
        "dcache_hit": 1,
        "dcache_miss": 10,
        "interconnect_req": 1,
        "interconnect_resp": 1,
    },
    "performance": {
        "description": "High-throughput 5-stage core with aggressive prediction",
        "xlen": 0,
        "misa": "gcbv",
        "pipeline_type": "five-stage",
        "enable_forwarding": True,
        "mul_latency": 1,
        "div_latency": 8,
        "fp_alu_latency": 2,
        "fp_div_latency": 8,
        "branch_mispredict_penalty": 2,
        "cycle_counter_start_delay": 0,
        "csr_flush_penalty": 2,
        "fence_flush_penalty": 2,
        "bpred_type": "gshare",
        "btb_entries": 1024,
        "bht_entries": 4096,
        "ras_entries": 32,
        "ghr_bits": 12,
        "pc_shift": 1,
        "enable_btb": True,
        "enable_ras": True,
        "untagged_btb": False,
        "registered_btb_read": False,
        "bht_initial_state": 0,
        "icache_capacity": 16384,
        "icache_ways": 4,
        "icache_line": 32,
        "icache_hit": 1,
        "icache_miss": 8,
        "dcache_capacity": 16384,
        "dcache_ways": 4,
        "dcache_line": 32,
        "dcache_hit": 1,
        "dcache_miss": 8,
        "interconnect_req": 1,
        "interconnect_resp": 1,
    },
    "rvcomp": {
        "description": "Archlab RVComp 5-stage educational RISC-V processor",
        "xlen": 32,
        "misa": "ima",
        "pipeline_type": "five-stage",
        "enable_forwarding": True,
        "mul_latency": 2,
        "div_latency": 34,
        "fp_alu_latency": 4,
        "fp_div_latency": 16,
        "branch_mispredict_penalty": 4,
        "cycle_counter_start_delay": 0,
        "csr_flush_penalty": 3,
        "fence_flush_penalty": 4,
        "bpred_type": "bimodal",
        "btb_entries": 512,
        "bht_entries": 8192,
        "ras_entries": 16,
        "ghr_bits": 10,
        "pc_shift": 2,
        "enable_btb": True,
        "enable_ras": False,
        "untagged_btb": True,
        "registered_btb_read": False,
        "bht_initial_state": 1,
        "icache_capacity": 16384,
        "icache_ways": 1,
        "icache_line": 32,
        "icache_hit": 1,
        "icache_miss": 1,
        "dcache_capacity": 16384,
        "dcache_ways": 1,
        "dcache_line": 32,
        "dcache_hit": 4,
        "dcache_miss": 1,
        "interconnect_req": 1,
        "interconnect_resp": 1,
    },
    "cfu-provingground": {
        "description": "Tokyo Tech Archlab CFU-ProvingGround FPGA processor",
        "xlen": 32,
        "misa": "im",
        "pipeline_type": "five-stage",
        "enable_forwarding": True,
        "mul_latency": 3,
        "div_latency": 18,
        "fp_alu_latency": 4,
        "fp_div_latency": 16,
        "branch_mispredict_penalty": 3,
        "cycle_counter_start_delay": 2,
        "csr_flush_penalty": 3,
        "fence_flush_penalty": 4,
        "bpred_type": "bimodal",
        "btb_entries": 2048,
        "bht_entries": 2048,
        "ras_entries": 16,
        "ghr_bits": 10,
        "pc_shift": 2,
        "enable_btb": True,
        "enable_ras": False,
        "untagged_btb": True,
        "registered_btb_read": True,
        "bht_initial_state": 0,
        "icache_capacity": 32768,
        "icache_ways": 1,
        "icache_line": 32,
        "icache_hit": 1,
        "icache_miss": 1,
        "dcache_capacity": 16384,
        "dcache_ways": 1,
        "dcache_line": 32,
        "dcache_hit": 1,
        "dcache_miss": 1,
        "interconnect_req": 1,
        "interconnect_resp": 1,
    }
}

def render_cfg(name: str, cfg: dict) -> str:
    lines = [
        f"# SimRV CPU Model Configuration: {name}",
        f"# Generated by SimRV CPU Model Wizard",
        "",
        "[cpu]",
        f'name = "{name}"',
        f'description = "{cfg["description"]}"',
    ]
    if cfg.get("xlen", 0):
        lines.append(f'xlen = {cfg["xlen"]}')
    lines.extend([
        f'misa = "{cfg["misa"]}"',
        "",
        "[pipeline]",
        f'type = "{cfg["pipeline_type"]}"',
        f'enable_forwarding = {"true" if cfg["enable_forwarding"] else "false"}',
        f'mul_latency = {cfg["mul_latency"]}',
        f'div_latency = {cfg["div_latency"]}',
        f'fp_alu_latency = {cfg["fp_alu_latency"]}',
        f'fp_div_latency = {cfg["fp_div_latency"]}',
        f'branch_mispredict_penalty = {cfg["branch_mispredict_penalty"]}',
        f'cycle_counter_start_delay = {cfg["cycle_counter_start_delay"]}',
        f'csr_flush_penalty = {cfg["csr_flush_penalty"]}',
        f'fence_flush_penalty = {cfg["fence_flush_penalty"]}',
        "",
        "[branch_predictor]",
        f'type = "{cfg["bpred_type"]}"',
        f'btb_entries = {cfg["btb_entries"]}',
        f'bht_entries = {cfg["bht_entries"]}',
        f'ras_entries = {cfg["ras_entries"]}',
        f'ghr_bits = {cfg["ghr_bits"]}',
        f'pc_shift = {cfg["pc_shift"]}',
        f'enable_btb = {"true" if cfg["enable_btb"] else "false"}',
        f'enable_ras = {"true" if cfg["enable_ras"] else "false"}',
        f'untagged_btb = {"true" if cfg["untagged_btb"] else "false"}',
        f'registered_btb_read = {"true" if cfg["registered_btb_read"] else "false"}',
        f'bht_initial_state = {cfg["bht_initial_state"]}',
        "",
        "[instruction_cache]",
        f'capacity_bytes = {cfg["icache_capacity"]}',
        f'associativity = {cfg["icache_ways"]}',
        f'line_bytes = {cfg["icache_line"]}',
        f'hit_latency = {cfg["icache_hit"]}',
        f'miss_latency = {cfg["icache_miss"]}',
        "",
        "[data_cache]",
        f'capacity_bytes = {cfg["dcache_capacity"]}',
        f'associativity = {cfg["dcache_ways"]}',
        f'line_bytes = {cfg["dcache_line"]}',
        f'hit_latency = {cfg["dcache_hit"]}',
        f'miss_latency = {cfg["dcache_miss"]}',
        "",
        "[interconnect]",
        f'request_latency = {cfg["interconnect_req"]}',
        f'response_latency = {cfg["interconnect_resp"]}',
        ""
    ])
    return "\n".join(lines)

def prompt_val(prompt: str, default):
    val = input(f"{prompt} [{default}]: ").strip()
    if not val:
        return default
    if isinstance(default, bool):
        return val.lower() in ("true", "1", "yes", "y", "on")
    if isinstance(default, int):
        return int(val)
    return val

def run_interactive(base_template: str, name: str) -> tuple[str, dict]:
    cfg = dict(TEMPLATES.get(base_template, TEMPLATES["balanced"]))
    print(f"\n=== SimRV CPU Model Configuration Wizard ===")
    print(f"Base template: {base_template}\n")

    name = prompt_val("Model identifier (name)", name)
    cfg["description"] = prompt_val("Description", cfg["description"])
    cfg["xlen"] = prompt_val("Supported XLEN (32, 64, or 0 for both)", cfg.get("xlen", 0))
    cfg["misa"] = prompt_val("MISA profile (e.g. gcbv, imac, ima, gc, im, i)", cfg["misa"])

    print("\n--- Pipeline & Execution Latencies ---")
    cfg["pipeline_type"] = prompt_val("Pipeline type (five-stage, three-stage)", cfg["pipeline_type"])
    cfg["enable_forwarding"] = prompt_val("Enable operand forwarding (true/false)", cfg["enable_forwarding"])
    cfg["mul_latency"] = prompt_val("Integer MUL latency (cycles)", cfg["mul_latency"])
    cfg["div_latency"] = prompt_val("Integer DIV latency (cycles)", cfg["div_latency"])
    cfg["branch_mispredict_penalty"] = prompt_val("Branch mispredict penalty (cycles)", cfg["branch_mispredict_penalty"])
    cfg["cycle_counter_start_delay"] = prompt_val("Cycle counter reset startup delay (cycles)", cfg["cycle_counter_start_delay"])

    print("\n--- Branch Predictor ---")
    cfg["bpred_type"] = prompt_val("Predictor type (static, bimodal, gshare, tournament, none)", cfg["bpred_type"])
    cfg["btb_entries"] = prompt_val("BTB entries", cfg["btb_entries"])
    cfg["bht_entries"] = prompt_val("BHT entries", cfg["bht_entries"])
    cfg["pc_shift"] = prompt_val("PC index shift (1 for 16-bit insts, 2 for 32-bit aligned)", cfg["pc_shift"])
    cfg["registered_btb_read"] = prompt_val("Registered BTB read (1 bubble on predict taken) (true/false)", cfg["registered_btb_read"])
    cfg["bht_initial_state"] = prompt_val("BHT 2-bit counter initial state (0: strong NT, 1: weak NT, 2: weak T, 3: strong T)", cfg["bht_initial_state"])

    print("\n--- L1 Caches & Memory ---")
    cfg["icache_capacity"] = prompt_val("Instruction cache capacity (bytes)", cfg["icache_capacity"])
    cfg["icache_ways"] = prompt_val("Instruction cache ways (associativity)", cfg["icache_ways"])
    cfg["icache_hit"] = prompt_val("Instruction cache hit latency (cycles)", cfg["icache_hit"])
    cfg["dcache_capacity"] = prompt_val("Data cache capacity (bytes)", cfg["dcache_capacity"])
    cfg["dcache_ways"] = prompt_val("Data cache ways (associativity)", cfg["dcache_ways"])
    cfg["dcache_hit"] = prompt_val("Data cache hit latency (cycles)", cfg["dcache_hit"])

    return name, cfg

TEMPLATE_ALIASES = {
    "five-stage": "balanced",
    "three-stage": "tiny",
    "5stage": "balanced",
    "3stage": "tiny",
}

def resolve_template(tpl: str) -> str:
    tpl_lower = tpl.lower()
def is_power_of_two(n: int) -> bool:
    return n > 0 and (n & (n - 1)) == 0

def validate_cfg(path: Path) -> bool:
    if not path.exists():
        print(f"\033[1;31m[ERROR]\033[0m File not found: {path}", file=sys.stderr)
        return False

    import configparser
    cp = configparser.ConfigParser()
    try:
        cp.read(path, encoding="utf-8")
    except Exception as e:
        print(f"\033[1;31m[ERROR]\033[0m Syntax error in {path}: {e}", file=sys.stderr)
        return False

    errors = []
    warnings = []

    # Check [cpu]
    if "cpu" not in cp:
        errors.append("Missing required section [cpu]")
    else:
        name = cp["cpu"].get("name", "").strip('"\'')
        if not name:
            errors.append("[cpu] 'name' is required and cannot be empty")
        xlen_str = cp["cpu"].get("xlen", "0")
        try:
            xlen = int(xlen_str)
            if xlen not in (0, 32, 64):
                errors.append(f"[cpu] invalid xlen={xlen} (must be 0, 32, or 64)")
        except ValueError:
            errors.append(f"[cpu] invalid integer for xlen: '{xlen_str}'")

        misa = cp["cpu"].get("misa", "").strip('"\'').lower()
        valid_misa = {"i", "im", "ima", "imac", "gc", "gcbv", "rv32i", "rv32im", "rv32ima", "rv32imac", "rv32gc", "rv32gcbv", "rv64i", "rv64im", "rv64ima", "rv64imac", "rv64gc", "rv64gcbv"}
        if misa and misa not in valid_misa:
            warnings.append(f"[cpu] unrecognized misa profile '{misa}'")

    # Check [pipeline]
    if "pipeline" in cp:
        pipe_type = cp["pipeline"].get("type", "").strip('"\'').lower()
        if pipe_type and pipe_type not in ("three-stage", "five-stage", "dual-issue", "3-stage", "5-stage"):
            errors.append(f"[pipeline] unknown pipeline type '{pipe_type}'")
        for lat_key in ("mul_latency", "div_latency"):
            if lat_key in cp["pipeline"]:
                try:
                    val = int(cp["pipeline"][lat_key])
                    if val < 1:
                        errors.append(f"[pipeline] {lat_key} must be >= 1 (got {val})")
                except ValueError:
                    errors.append(f"[pipeline] {lat_key} must be an integer")

    # Check [branch_predictor]
    if "branch_predictor" in cp:
        bp_type = cp["branch_predictor"].get("type", "").strip('"\'').lower()
        if bp_type and bp_type not in ("none", "static", "bimodal", "gshare", "tournament"):
            errors.append(f"[branch_predictor] unknown type '{bp_type}'")
        for ent_key in ("btb_entries", "bht_entries"):
            if ent_key in cp["branch_predictor"]:
                try:
                    val = int(cp["branch_predictor"][ent_key])
                    if not is_power_of_two(val):
                        warnings.append(f"[branch_predictor] {ent_key}={val} is not a power of two")
                except ValueError:
                    errors.append(f"[branch_predictor] {ent_key} must be an integer")

    # Check caches
    for cache_sec in ("instruction_cache", "data_cache"):
        if cache_sec in cp:
            sec = cp[cache_sec]
            if "capacity_bytes" in sec:
                try:
                    cap = int(sec["capacity_bytes"])
                    if cap > 0 and not is_power_of_two(cap):
                        warnings.append(f"[{cache_sec}] capacity_bytes={cap} is not a power of two")
                except ValueError:
                    errors.append(f"[{cache_sec}] capacity_bytes must be an integer")
            if "line_bytes" in sec:
                try:
                    line = int(sec["line_bytes"])
                    if line < 16 or not is_power_of_two(line):
                        errors.append(f"[{cache_sec}] line_bytes={line} must be a power of two >= 16")
                except ValueError:
                    errors.append(f"[{cache_sec}] line_bytes must be an integer")
            if "hit_latency" in sec:
                try:
                    hit = int(sec["hit_latency"])
                    if hit < 1:
                        errors.append(f"[{cache_sec}] hit_latency must be >= 1")
                except ValueError:
                    errors.append(f"[{cache_sec}] hit_latency must be an integer")

    print(f"\033[1;34m=== Validating CPU Model Configuration: {path} ===\033[0m")
    for w in warnings:
        print(f"  \033[1;33m[WARNING]\033[0m {w}")
    for e in errors:
        print(f"  \033[1;31m[ERROR]\033[0m   {e}")

    if errors:
        print(f"\n\033[1;31m[FAILED]\033[0m {len(errors)} error(s) found in {path.name}", file=sys.stderr)
        return False
    else:
        print(f"\n\033[1;32m[PASSED]\033[0m {path.name} is valid.")
        return True

def main():
    parser = argparse.ArgumentParser(description="SimRV CPU Model Scaffolding & Wizard")
    valid_choices = list(TEMPLATES.keys()) + list(TEMPLATE_ALIASES.keys())
    parser.add_argument("--template", choices=valid_choices, default="balanced",
                        help="Base template preset to scaffold from")
    parser.add_argument("--name", default="", help="Name of the model profile")
    parser.add_argument("--output", "-o", default="", help="Output .cfg file path")
    parser.add_argument("--non-interactive", action="store_true", help="Generate directly from template without prompts")
    parser.add_argument("--list-templates", action="store_true", help="List available template presets")
    parser.add_argument("--validate", "-v", default="", help="Validate an existing .cfg CPU model file")
    args = parser.parse_args()

    if args.validate:
        valid = validate_cfg(Path(args.validate))
        sys.exit(0 if valid else 1)

    if args.list_templates:
        print("Available base templates:")
        for k, v in TEMPLATES.items():
            print(f"  - {k:<18}: {v['description']}")
        return

    tpl_key = resolve_template(args.template)
    if tpl_key not in TEMPLATES:
        print(f"Error: unknown template '{args.template}'", file=sys.stderr)
        sys.exit(1)

    is_non_interactive = args.non_interactive or not sys.stdin.isatty() or (bool(args.name) and bool(args.output))
    name = args.name or (tpl_key if is_non_interactive else "custom_core")
    if is_non_interactive:
        cfg = dict(TEMPLATES[tpl_key])
    else:
        name, cfg = run_interactive(tpl_key, name)

    content = render_cfg(name, cfg)
    out_path = Path(args.output) if args.output else Path("configs/models") / f"{name}.cfg"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(content, encoding="utf-8")
    print(f"\n[+] Successfully wrote CPU model configuration to: {out_path.resolve()}")
    print(f"    Run with: SimRV --ca --cpu-profile {out_path} -m <program.elf>")

if __name__ == "__main__":
    main()
