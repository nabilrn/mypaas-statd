#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

make -C "$repo_root" install DESTDIR="$stage" >/dev/null

binary="$stage/usr/local/bin/mypaas-statd"
unit="$stage/usr/local/lib/systemd/system/mypaas-statd.service"

test -x "$binary"
test -f "$unit"

grep -Fq 'ExecStart=/usr/local/bin/mypaas-statd' "$unit"
grep -Fq 'RuntimeDirectory=mypaas' "$unit"
grep -Fq 'RuntimeDirectoryPreserve=yes' "$unit"
grep -Fq 'Environment=MYPAAS_STATD_SOCKET=/run/mypaas/statd.sock' "$unit"
grep -Fq 'ProtectControlGroups=yes' "$unit"
grep -Fq 'RestrictAddressFamilies=AF_UNIX' "$unit"

echo 'phase 4 packaging test passed'
