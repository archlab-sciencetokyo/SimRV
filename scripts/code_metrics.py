#!/usr/bin/env python3
"""
SimRV Codebase Metrics and Complexity Extraction Tool.
Analyzes C++ source and header files using Lizard to generate Markdown,
LaTeX, and JSON tables for academic papers, documentation, and audits.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Dict, List, Tuple

try:
    import lizard
except ImportError:
    print("Error: lizard module is required. Install via `pip install lizard`.", file=sys.stderr)
    sys.exit(1)


SUBSYSTEM_CONFIGS: Dict[str, Dict[str, Any]] = {
    "execute": {
        "description": "Vector, floating-point, integer execution units & ISA definitions",
        "paths": ["src/execute", "include/simrv/execute", "include/simrv/isa"],
    },
    "tui": {
        "description": "Modular TUI panes, modals, Sixel rendering & Virtual Terminal",
        "paths": ["src/tui", "include/simrv/tui"],
    },
    "core": {
        "description": "Architectural state, CPU, SBI, CSRs & Machine orchestration",
        "paths": ["src/core", "include/simrv/core", "include/simrv/xlen"],
    },
    "pipeline": {
        "description": "Instruction fetch/decode stages, decoder dispatch & pipeline logic",
        "paths": ["src/pipeline", "include/simrv/pipeline"],
    },
    "device": {
        "description": "VirtIO Console, Disk, Framebuffer, Audio, UART, RTC & Power models",
        "paths": ["src/device", "include/simrv/device"],
    },
    "debug": {
        "description": "GDB stub, Lockstep comparison, SymbolTable, BreakpointManager",
        "paths": ["src/debug", "include/simrv/debug"],
    },
    "memory": {
        "description": "Sv32/Sv39/Sv48 MMU, TileLink bus interconnect & memory hierarchy",
        "paths": ["src/memory", "include/simrv/memory", "include/simrv/cache"],
    },
    "util": {
        "description": "Instruction explainer routines, CLI parser & system helpers",
        "paths": ["src/util", "include/simrv/util"],
    },
}


@dataclass
class FunctionMetric:
    name: str
    long_name: str
    filename: str
    start_line: int
    end_line: int
    nloc: int
    cyclomatic_complexity: int
    token_count: int
    subsystem: str


@dataclass
class SubsystemMetric:
    name: str
    description: str
    files: int
    nloc: int
    functions: int
    tokens: int
    ccn_sum: int
    avg_ccn: float
    avg_nloc: float
    nloc_percent: float


@dataclass
class GlobalMetrics:
    total_files: int
    total_nloc: int
    total_functions: int
    total_tokens: int
    avg_function_nloc: float
    avg_function_ccn: float
    avg_function_tokens: float
    ccn_distribution: Dict[str, int]


def find_source_files(roots: List[str]) -> List[str]:
    """Find all C/C++ source and header files in the specified directories."""
    extensions = {".cpp", ".hpp", ".c", ".h", ".cc", ".cxx", ".hxx"}
    files: List[str] = []
    for root in roots:
        path = Path(root)
        if not path.exists():
            continue
        for p in path.rglob("*"):
            if p.is_file() and p.suffix in extensions:
                # Normalize relative path
                files.append(str(p.as_posix()))
    return sorted(files)


def classify_subsystem(filepath: str) -> str:
    """Map a file path to its architectural subsystem."""
    for sub, config in SUBSYSTEM_CONFIGS.items():
        for prefix in config["paths"]:
            if filepath.startswith(prefix):
                return sub
    return "top_level"


def analyze_codebase(roots: List[str]) -> Tuple[GlobalMetrics, List[SubsystemMetric], List[FunctionMetric]]:
    """Analyze the codebase using lizard and aggregate metrics."""
    files = find_source_files(roots)
    if not files:
        raise ValueError(f"No source files found in {roots}")

    all_functions: List[FunctionMetric] = []
    sub_data: Dict[str, Dict[str, Any]] = {
        sub: {
            "description": SUBSYSTEM_CONFIGS[sub]["description"],
            "files": 0,
            "nloc": 0,
            "functions": 0,
            "tokens": 0,
            "ccn_sum": 0,
        }
        for sub in SUBSYSTEM_CONFIGS
    }
    sub_data["top_level"] = {
        "description": "Top-level main entrypoint and driver",
        "files": 0,
        "nloc": 0,
        "functions": 0,
        "tokens": 0,
        "ccn_sum": 0,
    }

    total_nloc = 0
    total_tokens = 0
    ccn_dist = {"1-5 (Low)": 0, "6-10 (Moderate)": 0, "11-20 (High)": 0, "21+ (Complex)": 0}

    for f in files:
        sub = classify_subsystem(f)
        file_info = lizard.analyze_file(f)
        sub_data[sub]["files"] += 1
        sub_data[sub]["nloc"] += file_info.nloc
        sub_data[sub]["tokens"] += file_info.token_count
        total_nloc += file_info.nloc
        total_tokens += file_info.token_count

        for fn in file_info.function_list:
            sub_data[sub]["functions"] += 1
            sub_data[sub]["ccn_sum"] += fn.cyclomatic_complexity

            ccn = fn.cyclomatic_complexity
            if ccn <= 5:
                ccn_dist["1-5 (Low)"] += 1
            elif ccn <= 10:
                ccn_dist["6-10 (Moderate)"] += 1
            elif ccn <= 20:
                ccn_dist["11-20 (High)"] += 1
            else:
                ccn_dist["21+ (Complex)"] += 1

            all_functions.append(
                FunctionMetric(
                    name=fn.name,
                    long_name=fn.long_name,
                    filename=f,
                    start_line=fn.start_line,
                    end_line=fn.end_line,
                    nloc=fn.nloc,
                    cyclomatic_complexity=fn.cyclomatic_complexity,
                    token_count=fn.token_count,
                    subsystem=sub,
                )
            )

    total_functions = len(all_functions)
    avg_func_nloc = total_nloc / total_functions if total_functions else 0.0
    avg_func_ccn = sum(f.cyclomatic_complexity for f in all_functions) / total_functions if total_functions else 0.0
    avg_func_tokens = total_tokens / total_functions if total_functions else 0.0

    global_metrics = GlobalMetrics(
        total_files=len(files),
        total_nloc=total_nloc,
        total_functions=total_functions,
        total_tokens=total_tokens,
        avg_function_nloc=round(avg_func_nloc, 2),
        avg_function_ccn=round(avg_func_ccn, 2),
        avg_function_tokens=round(avg_func_tokens, 2),
        ccn_distribution=ccn_dist,
    )

    subsystem_metrics: List[SubsystemMetric] = []
    for sub, data in sub_data.items():
        if data["files"] == 0:
            continue
        funcs = data["functions"]
        avg_ccn = data["ccn_sum"] / funcs if funcs else 0.0
        avg_nloc = data["nloc"] / funcs if funcs else 0.0
        nloc_pct = (data["nloc"] / total_nloc * 100.0) if total_nloc else 0.0
        subsystem_metrics.append(
            SubsystemMetric(
                name=sub,
                description=data["description"],
                files=data["files"],
                nloc=data["nloc"],
                functions=funcs,
                tokens=data["tokens"],
                ccn_sum=data["ccn_sum"],
                avg_ccn=round(avg_ccn, 2),
                avg_nloc=round(avg_nloc, 2),
                nloc_percent=round(nloc_pct, 1),
            )
        )

    # Sort subsystems by NLOC descending
    subsystem_metrics.sort(key=lambda s: s.nloc, reverse=True)
    # Sort functions by CCN descending
    all_functions.sort(key=lambda f: f.cyclomatic_complexity, reverse=True)

    return global_metrics, subsystem_metrics, all_functions


def render_markdown(
    global_metrics: GlobalMetrics,
    subsystems: List[SubsystemMetric],
    functions: List[FunctionMetric],
    top_n: int = 10,
) -> str:
    """Render metrics as formatted GitHub Markdown."""
    lines: List[str] = []
    lines.append("# SimRV Codebase Complexity & Architecture Metrics\n")
    lines.append("## Global Codebase Statistics\n")
    lines.append(f"- **Total C++ Source & Header Files:** {global_metrics.total_files:,}")
    lines.append(f"- **Total Non-Comment Lines of Code (NLOC):** {global_metrics.total_nloc:,}")
    lines.append(f"- **Total Functions Analyzed:** {global_metrics.total_functions:,}")
    lines.append(f"- **Average Function NLOC:** {global_metrics.avg_function_nloc:.2f}")
    lines.append(f"- **Average Cyclomatic Complexity (CCN):** {global_metrics.avg_function_ccn:.2f}")
    lines.append(f"- **Average Tokens per Function:** {global_metrics.avg_function_tokens:.2f}\n")

    lines.append("### Complexity Distribution\n")
    lines.append("| Complexity Tier | Function Count | Percentage |")
    lines.append("| :--- | :--- | :--- |")
    for tier, count in global_metrics.ccn_distribution.items():
        pct = (count / global_metrics.total_functions * 100.0) if global_metrics.total_functions else 0.0
        lines.append(f"| CCN {tier} | {count:,} | {pct:.1f}% |")
    lines.append("")

    lines.append("## Subsystem Complexity Breakdown\n")
    lines.append("| Subsystem | Description | Files | NLOC | % Code | Functions | Avg CCN | Avg NLOC |")
    lines.append("| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |")
    for sub in subsystems:
        lines.append(
            f"| `{sub.name}` | {sub.description} | {sub.files} | {sub.nloc:,} | {sub.nloc_percent:.1f}% | "
            f"{sub.functions:,} | {sub.avg_ccn:.2f} | {sub.avg_nloc:.1f} |"
        )
    lines.append("")

    lines.append(f"## Top {top_n} Cyclomatic Complexity Hotspots\n")
    lines.append("| Rank | Function / Method | Subsystem | Location | CCN | NLOC | Tokens |")
    lines.append("| :--- | :--- | :--- | :--- | :--- | :--- | :--- |")
    for i, fn in enumerate(functions[:top_n], start=1):
        loc = f"`{fn.filename}:{fn.start_line}`"
        name = fn.name if len(fn.name) <= 45 else fn.name[:42] + "..."
        lines.append(f"| {i} | `{name}` | `{fn.subsystem}` | {loc} | **{fn.cyclomatic_complexity}** | {fn.nloc} | {fn.token_count} |")
    lines.append("")

    return "\n".join(lines)


def render_latex(
    global_metrics: GlobalMetrics,
    subsystems: List[SubsystemMetric],
    functions: List[FunctionMetric],
    top_n: int = 8,
) -> str:
    """Render metrics as publication-ready LaTeX tables using booktabs."""
    lines: List[str] = []

    # Table 1: Subsystem Breakdown
    lines.append("% --- LaTeX Table: Subsystem Complexity Breakdown ---")
    lines.append("\\begin{table*}[t]")
    lines.append("\\centering")
    lines.append("\\small")
    lines.append("\\caption{SimRV Subsystem Architectural Breakdown and Code Complexity Metrics}")
    lines.append("\\label{tab:simrv-subsystems}")
    lines.append("\\begin{tabular}{llrrrrrr}")
    lines.append("\\toprule")
    lines.append("\\textbf{Subsystem} & \\textbf{Primary Scope} & \\textbf{Files} & \\textbf{NLOC} & \\textbf{\\% Code} & \\textbf{Funcs} & \\textbf{Avg CCN} & \\textbf{Avg NLOC} \\\\")
    lines.append("\\midrule")
    for sub in subsystems:
        sub_name = sub.name.replace("_", "\\_")
        desc = sub.description.replace("&", "\\&").replace("_", "\\_")
        lines.append(
            f"\\texttt{{{sub_name}}} & {desc} & {sub.files} & {sub.nloc:,} & {sub.nloc_percent:.1f}\\% & "
            f"{sub.functions:,} & {sub.avg_ccn:.2f} & {sub.avg_nloc:.1f} \\\\"
        )
    lines.append("\\midrule")
    lines.append(
        f"\\textbf{{Total / Average}} & Entire Codebase & \\textbf{{{global_metrics.total_files}}} & "
        f"\\textbf{{{global_metrics.total_nloc:,}}} & 100.0\\% & \\textbf{{{global_metrics.total_functions:,}}} & "
        f"\\textbf{{{global_metrics.avg_function_ccn:.2f}}} & \\textbf{{{global_metrics.avg_function_nloc:.1f}}} \\\\"
    )
    lines.append("\\bottomrule")
    lines.append("\\end{tabular}")
    lines.append("\\end{table*}\n")

    # Table 2: Top Complexity Hotspots
    lines.append("% --- LaTeX Table: Top Complexity Hotspots ---")
    lines.append("\\begin{table}[t]")
    lines.append("\\centering")
    lines.append("\\small")
    lines.append(f"\\caption{{Top {top_n} Cyclomatic Complexity Hotspots in the SimRV Codebase}}")
    lines.append("\\label{tab:simrv-hotspots}")
    lines.append("\\begin{tabular}{llrrl}")
    lines.append("\\toprule")
    lines.append("\\textbf{Rank} & \\textbf{Function / Method} & \\textbf{CCN} & \\textbf{NLOC} & \\textbf{Subsystem} \\\\")
    lines.append("\\midrule")
    for i, fn in enumerate(functions[:top_n], start=1):
        clean_name = fn.name.replace("_", "\\_")
        if len(clean_name) > 35:
            clean_name = clean_name[:32] + "..."
        sub_name = fn.subsystem.replace("_", "\\_")
        lines.append(f"{i} & \\texttt{{{clean_name}}} & \\textbf{{{fn.cyclomatic_complexity}}} & {fn.nloc} & \\texttt{{{sub_name}}} \\\\")
    lines.append("\\bottomrule")
    lines.append("\\end{tabular}")
    lines.append("\\end{table}\n")

    return "\n".join(lines)


def render_json(
    global_metrics: GlobalMetrics,
    subsystems: List[SubsystemMetric],
    functions: List[FunctionMetric],
    top_n: int = 10,
) -> str:
    """Render metrics as machine-readable JSON."""
    data = {
        "global": asdict(global_metrics),
        "subsystems": [asdict(s) for s in subsystems],
        "top_hotspots": [asdict(f) for f in functions[:top_n]],
    }
    return json.dumps(data, indent=2)


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract SimRV codebase complexity metrics via Lizard.")
    parser.add_argument(
        "--format",
        choices=["markdown", "latex", "json", "summary"],
        default="markdown",
        help="Output format (default: markdown)",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=10,
        help="Number of top complexity functions to report (default: 10)",
    )
    parser.add_argument(
        "--output",
        "-o",
        type=str,
        help="Output file path (prints to stdout if omitted)",
    )
    parser.add_argument(
        "--dirs",
        nargs="+",
        default=["src", "include"],
        help="Directories to analyze (default: src include)",
    )

    args = parser.parse_args()

    try:
        global_metrics, subsystems, functions = analyze_codebase(args.dirs)
    except Exception as e:
        print(f"Error during analysis: {e}", file=sys.stderr)
        sys.exit(1)

    if args.format == "markdown":
        output = render_markdown(global_metrics, subsystems, functions, top_n=args.top)
    elif args.format == "latex":
        output = render_latex(global_metrics, subsystems, functions, top_n=args.top)
    elif args.format == "json":
        output = render_json(global_metrics, subsystems, functions, top_n=args.top)
    elif args.format == "summary":
        output = (
            f"SimRV Metrics: {global_metrics.total_files} files, {global_metrics.total_nloc:,} NLOC, "
            f"{global_metrics.total_functions:,} funcs, avg CCN={global_metrics.avg_function_ccn:.2f}, "
            f"avg NLOC={global_metrics.avg_function_nloc:.2f}"
        )

    if args.output:
        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(output + "\n")
        print(f"Metrics written to {args.output}")
    else:
        print(output)


if __name__ == "__main__":
    main()
