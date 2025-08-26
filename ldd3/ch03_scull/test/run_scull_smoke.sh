#!/usr/bin/env bash
# run_scull_smoke.sh — smarter CTest smoke test for scull
# Tries multiple strategies to discover the major:
#   1) From /sys/class/*/scull0/dev (preferred if device_create() used)
#   2) From /proc/devices by grepping SCULL_NAME (default: "scull")
#   3) From dmesg by grepping for "scull" and a "major" number
#
# Environment variables:
#   SCULL_DEV=/dev/scull0            # device node to test (default)
#   SCULL_NAME=scull                 # name to look for in /proc/devices
#   SCULL_MAJOR=<num>                # force a major number (skips discovery)
#   SCULL_MODULE=scull               # module name to modprobe/insmod (optional)
#   ALLOW_RESET=0|1                  # try IOCRESET first (may require CAP_SYS_ADMIN)
#
set -euo pipefail
BIN="${1:?need path to test_scull binary}"
DEV="${SCULL_DEV:-/dev/scull0}"
NAME="${SCULL_NAME:-scull}"
MAJOR="${SCULL_MAJOR:-}"
MOD="${SCULL_MODULE:-}"
RESET="${ALLOW_RESET:-0}"
SUDO_BIN="${SUDO_BIN:-}"        # set SUDO_BIN=sudo via CTest env if you need root for open()
log() { printf ">> %s\n" "$*"; }

# 0) Try to load module if requested
if [[ -n "$MOD" ]]; then
  if ! lsmod | awk '{print $1}' | grep -qx "$MOD"; then
    log "Attempting to load module: $MOD"
    if command -v modprobe >/dev/null 2>&1; then
      sudo modprobe "$MOD" || true
    fi
    # fallback: try insmod from build dir (common in local dev)
    if ! lsmod | awk '{print $1}' | grep -qx "$MOD"; then
      ko=$(find . .. ../../.. -maxdepth 3 -type f -name "$MOD.ko" 2>/dev/null | head -n1 || true)
      if [[ -n "$ko" ]]; then
        sudo insmod "$ko" || true
      fi
    fi
  fi
fi

# Helper: discover major from sysfs /sys/class/*/scull0/dev (contains "major:minor")
discover_major_sysfs() {
  local devnode="${DEV##*/}"    # scull0
  local base="${devnode%[0-9]*}" # scull
  # Scan likely class paths
  for p in /sys/class/"$base"/"$devnode"/dev /sys/class/*/"$devnode"/dev; do
    if [[ -r "$p" ]]; then
      cut -d: -f1 "$p"
      return 0
    fi
  done
  return 1
}

# Helper: discover major from /proc/devices by name
discover_major_proc() {
  awk -v name="$NAME" '$2==name {print $1}' /proc/devices
}

# Helper: discover major from dmesg lines mentioning "major"
discover_major_dmesg() {
  dmesg | tac | awk '
    BEGIN{maj=""}
    tolower($0) ~ /scull|'"$NAME"'/ && tolower($0) ~ /major/ {
      for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+$/) {maj=$i; print maj; exit}
    }'
}

ensure_devnode() {
  local major="$1"
  local node="$2"
  local dir=$(dirname "$node")
  if [[ ! -e "$dir" ]]; then sudo mkdir -p "$dir"; fi
  if [[ -e "$node" ]]; then sudo rm -f "$node"; fi
  sudo mknod "$node" c "$major" 0
  sudo chmod 666 "$node" || true
}

# 1) Ensure device node exists
if [[ ! -e "$DEV" ]]; then
  log "Device $DEV not found; attempting to create it"

  if [[ -z "$MAJOR" ]]; then
    # Try sysfs
    if MAJOR=$(discover_major_sysfs); then
      log "Found major via sysfs: $MAJOR"
    else
      # Try /proc/devices
      MAJOR=$(discover_major_proc || true)
      if [[ -n "$MAJOR" ]]; then
        log "Found major via /proc/devices ($NAME): $MAJOR"
      else
        # Try dmesg
        MAJOR=$(discover_major_dmesg || true)
        if [[ -n "$MAJOR" ]]; then
          log "Found major via dmesg: $MAJOR"
        fi
      fi
    fi
  fi

  if [[ -z "$MAJOR" ]]; then
    echo "ERROR: could not determine scull major. Provide SCULL_MAJOR=..., or ensure the module is loaded and exposes /proc/devices or /sys/class entries." >&2
    exit 2
  fi

  ensure_devnode "$MAJOR" "$DEV"
fi

ls -l "$DEV"

# 2) Optional reset
if [[ "$RESET" == "1" ]]; then
  log "Resetting device"
  if ! ${SUDO_BIN:+sudo} "$BIN" -d "$DEV" --reset; then
    echo "WARN: reset failed (needs CAP_SYS_ADMIN?); continuing" >&2
  fi
fi

# 3) Get/Set quantum  (NO tee, NO /tmp)
log "Get current quantum"
out=$(${SUDO_BIN:+sudo} "$BIN" -d "$DEV" --get-quantum)
cur_q=$(printf "%s\n" "$out" | awk '/Current quantum:/{print $3}')
log "Set quantum to 4096"
${SUDO_BIN:+sudo} "$BIN" -d "$DEV" --set-quantum 4096
log "Verify quantum == 4096"
out=$(${SUDO_BIN:+sudo} "$BIN" -d "$DEV" --get-quantum)
new_q=$(printf "%s\n" "$out" | awk '/Current quantum:/{print $3}')
[[ "$new_q" == "4096" ]] || { echo "FAIL: quantum expected 4096, got '$new_q'"; exit 3; }

# 4) Set/Get qset (NO tee, NO /tmp)
log "Set qset to 1000"
${SUDO_BIN:+sudo} "$BIN" -d "$DEV" --set-qset 1000
log "Verify qset == 1000"
out=$(${SUDO_BIN:+sudo} "$BIN" -d "$DEV" --get-qset)
new_qs=$(printf "%s\n" "$out" | awk '/Current qset:/{print $3}')
[[ "$new_qs" == "1000" ]] || { echo "FAIL: qset expected 1000, got '$new_qs'"; exit 4; }

# 5) Write/seek/read roundtrip
msg="hello"
log "Write payload: $msg"
${SUDO_BIN:+sudo} "$BIN" -d "$DEV" --write "$msg"
log "Seek to 0 and read 5"
out=$(${SUDO_BIN:+sudo} "$BIN" -d "$DEV" --seek 0 --read 5 | awk -F': ' '/^Read [0-9]+ bytes:/ {print $2; exit}')
[[ "$out" == "$msg" ]] || { echo "FAIL: readback expected '$msg', got '$out'"; exit 5; }

log "All smoke tests passed"
