#!/usr/bin/env bash
set -euo pipefail

IFACE="${IFACE:-netty0}"
COUNT="${COUNT:-50}"

BADUC="${BADUC:-02:11:22:33:44:55}"
MC1="${MC1:-01:00:5e:12:34:56}"
MCX="${MCX:-01:00:5e:66:77:88}"

PGDIR="/proc/net/pktgen"
KPG="${PGDIR}/kpktgend_0"
PGCTRL="${PGDIR}/pgctrl"

die(){ echo "ERROR: $*" >&2; exit 1; }
log(){ echo "[impair] $*"; }

need_root(){ [[ $EUID -eq 0 ]] || die "run as root"; }
need(){ command -v "$1" >/dev/null 2>&1 || die "missing $1"; }

stat_get(){ cat "/sys/class/net/$IFACE/statistics/$1"; }

snap_stats(){
  TXP=$(stat_get tx_packets)
  RXP=$(stat_get rx_packets)
  RXD=$(stat_get rx_dropped)
}

delta(){ echo $(( $1 - $2 )); }

expect_eq(){
  local name="$1" got="$2" want="$3"
  [[ "$got" -eq "$want" ]] || die "$name: got=$got want=$want"
}
expect_ge(){
  local name="$1" got="$2" want="$3"
  [[ "$got" -ge "$want" ]] || die "$name: got=$got want>=$want"
}

dmesg_mark(){
  DMESG_LINES0="$(dmesg | wc -l)"
}
dmesg_check_new(){
  # Look only at new lines since mark
  local new
  new="$(dmesg | tail -n +"$((DMESG_LINES0+1))" || true)"
  if echo "$new" | egrep -qi "BUG:|WARNING:|lockdep|preempt_count|scheduling while atomic|kernel BUG"; then
    echo "$new" >&2
    die "kernel warnings detected in dmesg (see above)"
  fi
}



pktgen_add_dev(){
  echo "add_device $IFACE" > "$KPG" 2>/dev/null || true
  [[ -f "${PGDIR}/${IFACE}" ]] || die "pktgen dev file missing: ${PGDIR}/${IFACE} (iface up?)"
}

pktgen_run(){
  local dst="$1" count="$2"
  local pgdev="${PGDIR}/${IFACE}"

  echo "count $count"  > "$pgdev"
  echo "pkt_size 64"   > "$pgdev"
  echo "clone_skb 0"   > "$pgdev"
  echo "delay 0"       > "$pgdev"
  echo "dst_mac $dst"  > "$pgdev"

  echo "start" > "$PGCTRL"
  sleep 0.3
}

bring_up(){
  ip link set dev "$IFACE" up
}
flags_off(){
  ip link set dev "$IFACE" promisc off || true
  ip link set dev "$IFACE" allmulticast off || true
}

tc1_unicast_ok(){
  local mac; mac="$(cat /sys/class/net/$IFACE/address)"
  log "TC1 unicast-ok dst=$mac"
  snap_stats; local tx0=$TXP rx0=$RXP rd0=$RXD
  pktgen_run "$mac" "$COUNT"
  snap_stats
  expect_ge "tx_packets delta" "$(delta "$TXP" "$tx0")" "$COUNT"
  expect_eq "rx_packets delta" "$(delta "$RXP" "$rx0")" "$COUNT"
  expect_eq "rx_dropped delta" "$(delta "$RXD" "$rd0")" "0"
}

tc2_unicast_bad_drop(){
  log "TC2 unicast-bad drop dst=$BADUC"
  snap_stats; local tx0=$TXP rx0=$RXP rd0=$RXD
  pktgen_run "$BADUC" "$COUNT"
  snap_stats
  expect_ge "tx_packets delta" "$(delta "$TXP" "$tx0")" "$COUNT"
  expect_eq "rx_packets delta" "$(delta "$RXP" "$rx0")" "0"
  expect_eq "rx_dropped delta" "$(delta "$RXD" "$rd0")" "$COUNT"
}

tc3_broadcast(){
  log "TC3 broadcast"
  snap_stats; local tx0=$TXP rx0=$RXP rd0=$RXD
  pktgen_run "ff:ff:ff:ff:ff:ff" "$COUNT"
  snap_stats
  expect_eq "rx_packets delta" "$(delta "$RXP" "$rx0")" "$COUNT"
  expect_eq "rx_dropped delta" "$(delta "$RXD" "$rd0")" "0"
}

tc4_mcast_drop_no_member(){
  log "TC4 multicast drop without membership dst=$MCX"
  flags_off
  ip maddr del "$MC1" dev "$IFACE" 2>/dev/null || true
  snap_stats; local rx0=$RXP rd0=$RXD
  pktgen_run "$MCX" "$COUNT"
  snap_stats
  expect_eq "rx_packets delta" "$(delta "$RXP" "$rx0")" "0"
  expect_eq "rx_dropped delta" "$(delta "$RXD" "$rd0")" "$COUNT"
}

tc5_mcast_accept_member(){
  log "TC5 multicast accept with membership dst=$MC1"
  flags_off
  ip maddr add "$MC1" dev "$IFACE" 2>/dev/null || true
  sleep 0.05
  snap_stats; local rx0=$RXP rd0=$RXD
  pktgen_run "$MC1" "$COUNT"
  snap_stats
  expect_eq "rx_packets delta" "$(delta "$RXP" "$rx0")" "$COUNT"
  expect_eq "rx_dropped delta" "$(delta "$RXD" "$rd0")" "0"
  ip maddr del "$MC1" dev "$IFACE" 2>/dev/null || true
}

tc6_allmulti(){
  log "TC6 allmulticast accept dst=$MCX"
  ip link set dev "$IFACE" allmulticast on
  snap_stats; local rx0=$RXP rd0=$RXD
  pktgen_run "$MCX" "$COUNT"
  snap_stats
  expect_eq "rx_packets delta" "$(delta "$RXP" "$rx0")" "$COUNT"
  expect_eq "rx_dropped delta" "$(delta "$RXD" "$rd0")" "0"
  ip link set dev "$IFACE" allmulticast off
}

tc7_promisc(){
  log "TC7 promisc accept dst=$BADUC"
  ip link set dev "$IFACE" promisc on
  snap_stats; local rx0=$RXP rd0=$RXD
  pktgen_run "$BADUC" "$COUNT"
  snap_stats
  expect_eq "rx_packets delta" "$(delta "$RXP" "$rx0")" "$COUNT"
  expect_eq "rx_dropped delta" "$(delta "$RXD" "$rd0")" "0"
  ip link set dev "$IFACE" promisc off
}

tc8_set_mac(){
  log "TC8 set mac"
  local newmac="02:aa:bb:cc:dd:ee"
  ip link set dev "$IFACE" down
  ip link set dev "$IFACE" address "$newmac"
  ip link set dev "$IFACE" up
  [[ "$(cat /sys/class/net/$IFACE/address)" == "$newmac" ]] || die "MAC not set"
  tc1_unicast_ok
}

tc9_mtu(){
  log "TC9 mtu change (expected fail with current driver)"
  set +e
  ip link set dev "$IFACE" mtu 1400
  local rc=$?
  set -e
  [[ $rc -ne 0 ]] || die "MTU change unexpectedly succeeded (your driver currently rejects changes)"
  ip link set dev "$IFACE" mtu 1500 2>/dev/null || true
}

tc10_updown(){
  log "TC10 up/down stress"
  for i in $(seq 1 20); do
    ip link set dev "$IFACE" down
    ip link set dev "$IFACE" up
  done
  tc1_unicast_ok
}

tc11_toggle_stress(){
  log "TC11 stress: flood + toggle flags"
  bring_up
  flags_off

  ( for i in $(seq 1 50); do
      ip link set dev "$IFACE" promisc on
      ip link set dev "$IFACE" promisc off
      ip link set dev "$IFACE" allmulticast on
      ip link set dev "$IFACE" allmulticast off
    done ) &

  pktgen_run "ff:ff:ff:ff:ff:ff" 20000
  wait
}

main(){
  need_root
  need ip
  [[ -d "/sys/class/net/$IFACE" ]] || die "iface $IFACE not found"

  bring_up
  dmesg_mark

  pktgen_add_dev
  flags_off

  case "${1:-all}" in
    all)
      tc1_unicast_ok
      tc2_unicast_bad_drop
      tc3_broadcast
      tc4_mcast_drop_no_member
      tc5_mcast_accept_member
      tc6_allmulti
      tc7_promisc
      tc8_set_mac
      tc9_mtu
      tc10_updown
      tc11_toggle_stress
      ;;
    1) tc1_unicast_ok ;;
    2) tc2_unicast_bad_drop ;;
    3) tc3_broadcast ;;
    4) tc4_mcast_drop_no_member ;;
    5) tc5_mcast_accept_member ;;
    6) tc6_allmulti ;;
    7) tc7_promisc ;;
    8) tc8_set_mac ;;
    9) tc9_mtu ;;
    10) tc10_updown ;;
    11) tc11_toggle_stress ;;
    *) die "usage: $0 [all|1..11] (env IFACE= COUNT=)" ;;
  esac

  dmesg_check_new
  ip -s link show dev "$IFACE" || true
  log "PASS"
}

main "$@"
