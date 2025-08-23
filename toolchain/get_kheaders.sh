#!/usr/bin/env bash
set -euo pipefail
# From WSL or any Linux shell on your PC
REMOTE=ml-gpu@192.168.4.116
KDIR_REMOTE=/usr/src/linux-headers-$(ssh $REMOTE uname -r)
DST=/mnt/c/Users/delaplai/git/cpp/ldd3-practice/toolchain/kheaders

rm -rf "$DST" && mkdir -p "$DST"
ssh "$REMOTE" "tar cz -C $KDIR_REMOTE include arch" | tar xz -C "$DST"
echo "Mirrored headers into: $DST"
