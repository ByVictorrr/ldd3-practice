#!/usr/bin/env bash
set -euo pipefail
mod="${1:?usage: $0 <module-name>}"
sudo sh -c "echo 'module $mod -p' > /sys/kernel/debug/dynamic_debug/control"
echo "[-] pr_debug disabled for $mod"
