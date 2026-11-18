# Custom guest root filesystem overlay

Set `custom_rootfs_overlay` in `linux-versions.conf` to this directory, or to another
directory of your choice. Its directory layout is copied into the guest root filesystem for
Alpine and Buildroot images. For example, `etc/motd` becomes `/etc/motd` in the guest.

Regular files are copied and symbolic links are preserved. Existing guest files at the same path
are replaced. Relative overlay paths are resolved from the version config file's directory.

Keep custom files in a separate overlay directory if you want to preserve them across updates to
the builder and its built-in templates.
