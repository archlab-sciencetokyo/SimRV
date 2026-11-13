#!/bin/sh
set -eu

: "${SIMRV_ASSET_BASE:?SIMRV_ASSET_BASE is required}"
: "${SIMRV_ARCH:?SIMRV_ARCH is required}"
: "${SIMRV_HOST_UID:?SIMRV_HOST_UID is required}"
: "${SIMRV_HOST_GID:?SIMRV_HOST_GID is required}"

case "$(uname -m)" in
    x86_64) host_arch=x86_64 ;;
    aarch64) host_arch=aarch64 ;;
    *) echo "Unsupported portable build host: $(uname -m)" >&2; exit 1 ;;
esac
test "$SIMRV_ARCH" = "$host_arch"

asset="${SIMRV_ASSET_BASE}.tar.gz"

restore_workspace_owner() {
    for path in build portable-package "$asset" portable-archive.list; do
        if [ -e "$path" ]; then
            chown -R "${SIMRV_HOST_UID}:${SIMRV_HOST_GID}" "$path"
        fi
    done
}
trap restore_workspace_owner EXIT

# Alpine provides a musl-native GCC toolchain. GCC 15 supplies the C++23
# headers; the static system libraries let the archive run without host glibc.
apk add --no-cache build-base boost-dev cmake file gettext-static glib-dev glib-static \
    linux-headers meson ninja pcre2-static pkgconf python3 tar wget zlib-static

wget -q https://gitlab.freedesktop.org/slirp/libslirp/-/archive/v4.9.5/libslirp-v4.9.5.tar.bz2 \
    -O /tmp/libslirp-v4.9.5.tar.bz2
echo '4f59df896cb345ea76d7f68b1e820872feaa9d8255a6761f6bf8a0f2d5144bcd  /tmp/libslirp-v4.9.5.tar.bz2' \
    | sha256sum -c -
tar -xjf /tmp/libslirp-v4.9.5.tar.bz2 -C /tmp
meson setup /tmp/libslirp-build /tmp/libslirp-v4.9.5 \
    --prefix=/usr/local --buildtype=release --default-library=static -Dstatic=true
ninja -C /tmp/libslirp-build install
# libslirp's Meson build creates a static_library when `static=true`, but that
# target is not marked for installation. Stage it explicitly so CMake's
# pkg-config dependency resolves to the archive instead of a missing library.
install -Dm644 /tmp/libslirp-build/libslirp.a /usr/local/lib/libslirp.a
# Meson/GNU ar emits a thin archive whose members are relative to the archive.
# Keep its member directory beside the staged archive so the linker can resolve it.
cp -a /tmp/libslirp-build/libslirp.a.p /usr/local/lib/
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

cmake -S . -B build/rv64-musl-release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DSIMRV_XLEN=64 \
    -DSIMRV_X86_64_V3_BASELINE=OFF \
    -DSIMRV_NATIVE_HOST_OPTIMIZATIONS=OFF \
    -DSIMRV_WARNINGS_AS_ERRORS=ON \
    -DSIMRV_ALLOW_UNQUALIFIED_TOOLCHAIN=ON \
    -DPKG_CONFIG_ARGN=--static \
    -DCMAKE_PROJECT_SimRV_INCLUDE="${PWD}/.github/scripts/static-library-search.cmake" \
    -DCMAKE_EXE_LINKER_FLAGS="-L/usr/local/lib -static -static-libgcc -static-libstdc++"
cmake --build build/rv64-musl-release --target SimRV simrv-release-check --parallel 2

file build/rv64-musl-release/simrv | grep -E 'statically linked|static-pie linked'
if readelf -d build/rv64-musl-release/simrv | grep -q ' NEEDED '; then
    echo "Portable archive binary has dynamic library dependencies" >&2
    exit 1
fi

mkdir -p portable-package
cpack --config build/rv64-musl-release/CPackConfig.cmake -G TGZ \
    -D CPACK_COMPONENTS_ALL=Runtime -B portable-package
archive_count="$(find portable-package -maxdepth 1 -type f -name '*.tar.gz' | wc -l | tr -d ' ')"
test "$archive_count" -eq 1
archive="$(find portable-package -maxdepth 1 -type f -name '*.tar.gz' | head -n 1)"
mv "$archive" "$asset"
tar -tzf "$asset" > portable-archive.list
grep -q '^usr/share/SimRV/SimRVBuildMetadata.json$' portable-archive.list
grep -q '^usr/share/SimRV/schemas/simrv-build-metadata.schema.json$' portable-archive.list
build/rv64-musl-release/simrv-release-check --binary build/rv64-musl-release/simrv
