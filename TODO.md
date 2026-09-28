# SimRV 3.0 beta.3 handoff

Branch: `release/3.0.0-beta.3`
Target: `dev`

The beta.3 candidate consolidates the public RV64-capable runtime, native package bundles, and
stable/development documentation. Do not create the tag until release CI is green.

Release qualification:

1. Run the RV32 and RV64 gate suites and `scripts/release_check.py`.
2. Validate the portable TGZ, RPM repository bundle, and DEB repository bundle.
3. Confirm clean package installs and `simrv --version` on Fedora and Debian environments.
4. Merge this release branch into `dev`, then create and push `v3.0.0-beta.3`.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.
