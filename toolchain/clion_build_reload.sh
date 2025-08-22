#!/usr/bin/env bash
set -euo pipefail
# Build then (re)load kernel module(s) in a directory.
# Usage:
#   clion_build_reload.sh [-a | -m <name>] <remote-module-dir> [KDIR]
#     -a            load ALL .ko in dep order (based on modinfo depends)
#     -m <name>     load only this module (without .ko)
# Notes (no -a/-m):
#   - if exactly one .ko exists -> use it
#   - else if <dir>/<basename(dir)>.ko exists -> use it
#   - else newest .ko

usage(){ echo "usage: $(basename "$0") [-a | -m <name>] <dir> [KDIR]"; exit 1; }

all=false; pick=""
while getopts ":am:" opt; do
  case "$opt" in
    a) all=true ;;
    m) pick="$OPTARG" ;;
    *) usage ;;
  esac
done
shift $((OPTIND-1))

dir="${1:?$(usage)}"; shift || true
dir="${dir//\\//}"   # normalize backslashes if any
KDIR="${1:-${KDIR:-/lib/modules/$(uname -r)/build}}"

SUDO="sudo"; $SUDO -n true >/dev/null 2>&1 && SUDO="sudo -n" || true

echo "[i] Building in: $dir  (KDIR=$KDIR)"
( cd "$dir" && make -j"$(nproc)" KDIR="$KDIR" )

cd "$dir"

# All .ko here
mapfile -t KOS < <(ls -1 *.ko 2>/dev/null || true)
((${#KOS[@]})) || { echo "ERR: no .ko in $PWD"; exit 1; }

mname(){ basename "${1%.ko}"; }

# Build local dependency map (only deps that are also built here)
declare -A PRESENT DEPS
for f in "${KOS[@]}"; do
  n="$(mname "$f")"; PRESENT["$n"]=1
done
for f in "${KOS[@]}"; do
  n="$(mname "$f")"
  deps="$(modinfo -F depends "$f" 2>/dev/null || true)"
  list=()
  IFS=',' read -r -a arr <<<"$deps"
  for d in "${arr[@]}"; do
    d="${d//[[:space:]]/}"
    [[ -n "$d" && -n "${PRESENT[$d]:-}" ]] && list+=("$d")
  done
  DEPS["$n"]="${list[*]}"
done

# Helper: unload all local modules (best effort)
unload_all(){
  local names=()
  for f in "${KOS[@]}"; do names+=("$(mname "$f")"); done

  # 1) try modprobe -r on the whole set
  if ! $SUDO modprobe -r "${names[@]}" 2>/dev/null; then
    # 2) reverse topo: remove modules that no one depends on (locally)
    local remaining=("${!PRESENT[@]}")
    local changed
    for _ in {1..10}; do
      changed=false
      local next=()
      for n in "${remaining[@]}"; do
        # does any other remaining module depend on n?
        local has_dependent=false
        for m in "${remaining[@]}"; do
          [[ "$m" == "$n" ]] && continue
          [[ " ${DEPS[$m]} " == *" $n "* ]] && { has_dependent=true; break; }
        done
        if ! $has_dependent; then
          $SUDO rmmod "$n" >/dev/null 2>&1 || true
          changed=true
        else
          next+=("$n")
        fi
      done
      remaining=("${next[@]}")
      $changed || break
      ((${#remaining[@]})) || break
    done
  fi

  # report leftovers (likely held by external users)
  for n in "${!PRESENT[@]}"; do
    if lsmod | awk '{print $1}' | grep -qx "$n"; then
      echo "[!] $n is still loaded (busy). Holders:"
      ls -1 "/sys/module/$n/holders" 2>/dev/null || echo "(none listed)"
    fi
  done
}

if $all; then
  echo "[i] Loading ALL modules in dependency order"
  unload_all

  # greedy topo load: load when all local deps are present
  remaining=("${KOS[@]}")
  for _ in {1..10}; do
    progress=false; next=()
    for f in "${remaining[@]}"; do
      n="$(mname "$f")"
      ok=true
      for d in ${DEPS[$n]}; do
        lsmod | awk '{print $1}' | grep -qx "$d" || { ok=false; break; }
      done
      if $ok; then
        echo "[+] insmod $f"
        $SUDO insmod "./$f" || echo "[~] insmod $f failed"
        progress=true
      else
        next+=("$f")
      fi
    done
    remaining=("${next[@]}")
    $progress || break
    ((${#remaining[@]}==0)) && break
  done

  if ((${#remaining[@]})); then
    echo "[!] Could not resolve a clean order; best-effort load of remaining:"
    for f in "${remaining[@]}"; do echo "[~] insmod $f"; $SUDO insmod "./$f" || true; done
  fi

else
  # single target selection
  if [[ -n "$pick" ]]; then
    [[ -f "$pick.ko" ]] || { echo "ERR: $pick.ko not found in $PWD"; exit 1; }
    target="$pick.ko"
  else
    if ((${#KOS[@]}==1)); then
      target="${KOS[0]}"
    else
      base="$(basename "$PWD")"
      if [[ -f "$base.ko" ]]; then target="$base.ko"
      else target="$(ls -t *.ko | head -n1)"; fi
    fi
  fi
  name="$(mname "$target")"
  echo "[i] Target module: $target"
  $SUDO modprobe -r "$name" >/dev/null 2>&1 || true
  if lsmod | awk '{print $1}' | grep -qx "$name"; then
    echo "[!] $name still loaded (busy) — cannot reload"
    ls -1 "/sys/module/$name/holders" 2>/dev/null || true
    exit 1
  fi
  echo "[+] insmod $target"
  $SUDO insmod "./$target"
fi

# Logs
if dmesg -T >/dev/null 2>&1; then dmesg | tail -n 60
else $SUDO journalctl -k -n 60 --no-pager
fi
