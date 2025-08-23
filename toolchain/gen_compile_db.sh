#!/usr/bin/env bash
set -euo pipefail
# Generate a merged compile_commands.json for all chapter modules.
# Usage: gen_compile_db.sh [PROJECT_ROOT] [KDIR]
ROOT="${1:-$HOME/CLionProjects/ldd3-practice}"
KDIR="${2:-${KDIR:-/lib/modules/$(uname -r)/build}}"
OUT="$ROOT/compile_commands.json"

command -v bear >/dev/null || { echo "bear not installed"; exit 1; }
command -v jq >/dev/null  || { echo "jq not installed"; exit 1; }

# Find Kbuild module dirs (= Makefile containing 'obj-m +=')
mapfile -t MODDIRS < <(grep -rl --include=Makefile -E "^[[:space:]]*obj-m[[:space:]]*\\+=" "$ROOT/ldd3" 2>/dev/null \
                      | xargs -r -n1 dirname | sort -u)

if ((${#MODDIRS[@]} == 0)); then
  echo "No Kbuild module dirs found under $ROOT/ldd3"
  exit 1
fi

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

i=0
for d in "${MODDIRS[@]}"; do
  echo "[*] capturing $d"
  # 1) clean first so we always get compile commands
  make -s -C "$KDIR" M="$d" clean || true

  # 2) capture compile commands; keep going even if modpost fails
  ( cd "$d" && bear --output "$TMPDIR/cc_$i.json" \
      -- make -k -j"$(nproc)" -C "$KDIR" M="$d" V=1 modules ) || true
  i=$((i+1))
done


# Merge all per-dir DBs. Remove empties; unique by (file, directory, command) best-effort.
mapfile -t PARTS < <(ls -1 "$TMPDIR"/cc_*.json 2>/dev/null || true)
if ((${#PARTS[@]} == 0)); then
  echo "No compile DB fragments produced."
  exit 1
fi

jq -s '
  # flatten all arrays, drop nulls, then best-effort unique
  [ .[] | select(type=="array") | .[] ]
  | unique_by( (.file // "") + "|" + (.directory // "") + "|" + (.command // (.arguments|tostring)) )
' "${PARTS[@]}" > "$OUT.tmp"

mv "$OUT.tmp" "$OUT"
echo "[ok] wrote $OUT"