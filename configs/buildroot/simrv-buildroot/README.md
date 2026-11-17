# Generic SimRV Buildroot baseline

This Buildroot external tree provides a small serial-console Linux root filesystem for the SimRV virtual platform. It enables DHCP and a root shell, and is intended as an editable starting point for user-specific packages and overlays. It contains no lab-specific drivers or applications.

Edit the architecture defconfig and `rootfs-overlay/` files before building.
