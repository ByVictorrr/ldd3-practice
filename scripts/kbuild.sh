#!/usr/bin/env bash
set -euo pipefail
# Usage: ./scripts/kbuild.sh ldd3/ch02_hello [KDIR]
moddir="${1:?usage: $0 <relative-module-dir> [KDIR]  e.g., ldd3/ch02_hello}"
KDIR="${2:-${KDIR:-/lib/modules/$(uname -r)/build}}"
echo "[i] Building in $moddir (KDIR=$KDIR)"
( cd "$moddir" && make -j"$(nproc)" KDIR="$KDIR" )
