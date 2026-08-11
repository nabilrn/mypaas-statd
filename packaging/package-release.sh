#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-}"
binary="${BINARY:-$repo_root/build/mypaas-statd}"
out_dir="${OUT_DIR:-$repo_root/dist}"

if [[ -z "$version" ]]; then
  echo "usage: $0 <version>" >&2
  exit 2
fi
if [[ ! "$version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9._-]+)?$ ]]; then
  echo "version must look like v0.1.0 or v0.1.0-rc.1" >&2
  exit 2
fi
if [[ ! -x "$binary" ]]; then
  echo "release binary is missing or not executable: $binary" >&2
  exit 1
fi

case "$(uname -m)" in
  x86_64|amd64)
    arch="amd64"
    ;;
  *)
    echo "unsupported release host architecture: $(uname -m); v0.1 packaging currently supports linux-amd64 only" >&2
    exit 1
    ;;
esac

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

install -Dm0755 "$binary" "$stage/mypaas-statd"
install -Dm0644 "$repo_root/packaging/mypaas-statd.service" "$stage/mypaas-statd.service"
printf '%s\n' "$version" > "$stage/VERSION"

mkdir -p "$out_dir"
artifact="mypaas-statd-linux-${arch}.tar.gz"
tar -C "$stage" -czf "$out_dir/$artifact" mypaas-statd mypaas-statd.service VERSION
(
  cd "$out_dir"
  sha256sum "$artifact" > SHA256SUMS
)

printf 'created %s\n' "$out_dir/$artifact"
printf 'created %s\n' "$out_dir/SHA256SUMS"
