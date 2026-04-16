#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build/ninja-clang-release}"

if [[ ! -f "${build_dir}/compile_commands.json" ]]; then
  echo "clang-tidy: missing ${build_dir}/compile_commands.json; configure/build first" >&2
  exit 1
fi

clang_tidy_bin="${CLANG_TIDY_BIN:-}"
if [[ -z "${clang_tidy_bin}" ]]; then
  for candidate in clang-tidy-22 clang-tidy-21 clang-tidy-20 clang-tidy; do
    if command -v "${candidate}" >/dev/null 2>&1; then
      clang_tidy_bin="${candidate}"
      break
    fi
  done
fi

if [[ -z "${clang_tidy_bin}" ]]; then
  echo "clang-tidy: no clang-tidy binary found; set CLANG_TIDY_BIN to override" >&2
  exit 1
fi

mapfile -t sources < <(find src -type f -name '*.cpp' | sort)
if [[ ${#sources[@]} -eq 0 ]]; then
  echo "clang-tidy: no source files found" >&2
  exit 1
fi

jobs="${CLANG_TIDY_JOBS:-$(nproc)}"
echo "clang-tidy: using ${clang_tidy_bin} on ${#sources[@]} files (jobs=${jobs})"

printf '%s\n' "${sources[@]}" | xargs -P "${jobs}" -n 1 "${clang_tidy_bin}" -p "${build_dir}"

echo "clang-tidy: completed"
