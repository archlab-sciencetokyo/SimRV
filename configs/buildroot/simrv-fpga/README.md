# SimRV FPGA Buildroot profile

This external-tree overlay is intentionally headless. It provides the
conventional BusyBox init/getty, DHCP, fstab, and identity files needed for a
small virt-compatible FPGA Linux baseline. The Alpine profile remains the
supported JWM/X11 GUI image. The rootfs uses Buildroot's pinned internal musl
toolchain, so its output is not affected by the host's cross-compiler version.
