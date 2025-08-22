#!/usr/bin/env bash
set -euo pipefail

modname="${1:-}"
if [[ -z "$modname" ]]; then
    echo "Usage: $0 <module-name>"
    exit 1
fi

echo "[+] rmmod $modname"
sudo rmmod "$modname"
dmesg | tail -n 20
