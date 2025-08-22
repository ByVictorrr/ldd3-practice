#!/usr/bin/env bash
set -euo pipefail

modname="${1:-}"
if [[ -z "$modname" ]]; then
    echo "Usage: $0 <module-name> [path]"
    exit 1
fi

# Path defaults to current dir’s .ko, but can override
modpath="${2:-$(dirname "$0")/../ldd3/$modname/$modname.ko}"

if [[ ! -f "$modpath" ]]; then
    echo "Error: module file not found: $modpath"
    exit 1
fi

echo "[+] insmod $modpath"
sudo insmod "$modpath"
dmesg | tail -n 40
