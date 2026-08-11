#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="$(mktemp -d)"
extract_dir="$(mktemp -d)"
trap 'rm -rf "$out_dir" "$extract_dir"' EXIT

OUT_DIR="$out_dir" bash "$repo_root/packaging/package-release.sh" v0.1.0-test >/dev/null

artifact="$out_dir/mypaas-statd-linux-amd64.tar.gz"
test -f "$artifact"
test -f "$out_dir/SHA256SUMS"
(
  cd "$out_dir"
  sha256sum -c SHA256SUMS >/dev/null
)

tar -C "$extract_dir" -xzf "$artifact"
test -x "$extract_dir/mypaas-statd"
test -f "$extract_dir/mypaas-statd.service"
test "$(cat "$extract_dir/VERSION")" = "v0.1.0-test"
grep -Fq 'ExecStart=/usr/local/bin/mypaas-statd' "$extract_dir/mypaas-statd.service"

echo 'release packaging test passed'
