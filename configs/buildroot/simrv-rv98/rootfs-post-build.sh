#!/bin/sh
set -eu

target_dir=${1:?Buildroot target directory is required}
inittab="$target_dir/etc/inittab"

if [ ! -f "$inittab" ]; then
    echo "Buildroot inittab not found: $inittab" >&2
    exit 1
fi

if ! grep -q '^tty1::respawn:' "$inittab"; then
    printf '%s\n' 'tty1::respawn:/sbin/getty -L 38400 tty1 vt100' >> "$inittab"
fi

chmod 0755 "$target_dir/usr/local/bin/doom"
