# RTL parity extensions

SimRV includes a generic parity runner that compares retirement traces and cycle counters from a
reference target with SimRV. Target-specific build and execution logic is supplied by an optional
shared-library plugin. The simulator does not include a hardware-project adapter by default.

The runner also supports a user-editable CPU/SoC model file with `--config` and writes its work and
trace artifacts to caller-selected directories:

```sh
simrv-parity --plugin ./libmy-reference.so --list-targets
simrv-parity --plugin ./libmy-reference.so my-reference \
  --config ./my-platform.cfg --work-dir ./parity-work --trace-dir ./parity-traces
```

Use `simrv-parity --tune` to enable calibration. Tuning uses the target plugin and selected `.cfg`
file; review the resulting values before applying them.

## Parity plugin ABI

Include `simrv/rtl/ParityPlugin.h` from the SimRV development package. A plugin exports
`simrv_rtl_plugin_init()` and returns a descriptor whose `abi_version` is
`SIMRV_RTL_PLUGIN_ABI_VERSION` and whose `struct_size` is `sizeof(SimRvRtlTarget)`. The descriptor
provides a stable target name, description, preparation callback, per-benchmark execution callback,
and optional cleanup callback. Results report both reference and SimRV cycles/instruction counts.

The C ABI keeps target implementation details outside SimRV and allows a plugin to be built in a
separate project against the matching SimRV SDK. Do not expose C++ classes across this boundary.

## Measurements

The runner distinguishes architectural agreement (retired instruction stream) from timing agreement
(cycle counts). A cycle delta alone is not an architectural failure; a retirement mismatch is. Each
target adapter is responsible for reporting unavailable or invalid counters as an error instead of
substituting guessed values.
