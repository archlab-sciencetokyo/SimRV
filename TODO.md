# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.5`
Target: `dev`

This RC consolidates the public RV64-capable runtime, native package bundles, static musl portable
archive, supported-distro checks, attachable Unix-socket TUI, capability diagnostics, versioned
SoC manifests, and the profile-to-preset terminology cleanup. Do not create the tag until release
CI is green.

Release qualification:

1. Run the RV32 and RV64 gate suites and `scripts/release_check.py`.
2. Validate the static portable TGZ, RPM repository bundle, and DEB repository bundle.
3. Confirm clean package installs and `simrv --version` across the supported distro matrix.
4. Soak attach/detach, reconnect, reboot lifecycle notifications, and non-TTY output.
5. Run the GA smoke workflow on both native binaries and validate the exported SoC manifest schema.
6. Merge this release branch into `dev`, then create and push `v3.0.0-rc.5` as a prerelease.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.
