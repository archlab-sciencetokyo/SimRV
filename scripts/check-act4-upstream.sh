#!/usr/bin/env bash
set -euo pipefail

expected_revision="${1:?usage: check-act4-upstream.sh <resolved-upstream-revision>}"
upstream_url="${2:-https://github.com/riscv/riscv-arch-test.git}"
upstream_branch="${3:-refs/heads/act4}"

actual_revision="$(git ls-remote --exit-code "$upstream_url" "$upstream_branch" | cut -f1)"
if [[ "$actual_revision" != "$expected_revision" ]]; then
  printf 'Resolved ACT4 revision no longer matches upstream.\n' >&2
  printf 'Resolved: %s\n' "$expected_revision" >&2
  printf 'Upstream: %s\n' "${actual_revision:-unavailable}" >&2
  exit 1
fi

printf 'Resolved ACT4 revision matches upstream %s at %s.\n' "$upstream_branch" "$actual_revision"
