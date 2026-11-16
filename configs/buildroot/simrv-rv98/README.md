# RV98 Buildroot Doom image

This RV32IMA Buildroot root filesystem is intended to replace the ext4 rootfs
inside the existing RV98 full-system image. It keeps RV98's custom RVComp
kernel, initramfs, DTB, MMC, UART, Ethernet and keyboard drivers. The rootfs is
written at the existing 32 MiB MMC offset in a derived image, leaving the
source `sys_full.img` untouched.

RV98 exposes a simple framebuffer (`/dev/fb0`) and has no DRM GPU or audio
device. Doom therefore uses PrBoom with SDL 1.2's fbcon driver and dummy audio.
The image includes the shareware Doom I WAD and provides the `doom` command.
Its `openvt` command has an alarm-based fallback for RV98's kernel, which does
not provide POSIX timers.

Build from the repository root:

```sh
scripts/build-rv98-doom-image.sh
```

Launch:

```sh
simrv --config configs/models/rvcomp.cfg \
  --dtb linux-images/rv98/rv32/doom/rv98.dtb \
  -m linux-images/rv98/rv32/doom/sys_full.img
```

At the shell, run `doom`. To use tty2, run `openvt -c 2 -s -- doom`.
