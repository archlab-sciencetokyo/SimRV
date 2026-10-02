# VS Code Launcher & Debugging

SimRV includes a Linux-first VS Code extension under `tools/vscode-simrv`. The extension launches
SimRV in CLI/GDB mode and connects VS Code's Microsoft C/C++ debugger to the existing GDB Remote
Serial Protocol server. It does not start the SimRV TUI.

## Prerequisites

- VS Code with Microsoft's **C/C++** extension (`ms-vscode.cpptools`).
- A built SimRV executable.
- A RISC-V GDB executable, normally `riscv64-unknown-elf-gdb`.
- Node.js/npm when developing the extension itself.

## Configure a workspace

Create `.vscode/simrv.json`:

```json
{
  "simrv": "${workspaceFolder}/build/rv64-release/simrv",
  "program": "${workspaceFolder}/build/guest.elf",
  "gdb": "riscv64-unknown-elf-gdb",
  "args": ["--baremetal"],
  "stopAtEntry": true,
  "preLaunchTask": "build-guest"
}
```

`program` is passed to SimRV as `-m`; `args` contains additional simulator options. Paths can be
absolute or relative to the workspace.

## Commands

- Click **SimRV Debug** in the status bar, or the SimRV action button in a C/C++/assembly editor,
  for a one-click debug launch.
- Run **SimRV: Create/Update Debug Profile** to generate the native **Run and Debug** entry from
  `simrv.json`. It creates **SimRV Guest (IA)** and **SimRV Guest (CA)** profiles, preserves
  unrelated entries in `.vscode/launch.json`, and updates only generated SimRV profiles. Set
  `preLaunchTask` in `simrv.json` to compile the ELF before F5. Use CA for the pipeline view.

- Source breakpoints work when the ELF contains debug information (`-g`); assembly breakpoints
  can be set by symbol or address. The C/C++ extension provides the normal stack, variables,
  memory, register, stepping, and breakpoint views. The included hello guest build task uses
  `-g -O0`, so opening `examples/hello/hello.S` and setting a breakpoint before pressing F5
  should bind it to the guest ELF.
- **SimRV: Run** starts the simulator and opens the `SimRV Guest` integrated terminal.
- **SimRV: Debug** starts SimRV with an ephemeral GDB port and attaches `cppdbg`.
- **SimRV: Stop** terminates the active simulator.

The debugger uses the normal VS Code C/C++ views for breakpoints, stepping, variables, memory,
call stack, and registers supported by GDB. The extension contributes a **SimRV Guest Status**
panel for simulator-specific run state and a **SimRV Pipeline** panel. The pipeline panel displays
the current fetch, decode, execute, memory, writeback, and retired stages when cycle-accurate mode
is enabled; it reports when the selected execution mode has no pipeline snapshot.
