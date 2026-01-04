Here’s an **intermediate→hard “utility” network driver project** you can implement as a Linux kernel module (no real NIC needed), plus a **hard-mode variant** that targets real(ish) hardware in QEMU.

---

## Project: `netimpair` — a utility virtual NIC that adds delay/loss + optional VLAN rewrite

### Goal

Create a virtual Ethernet interface (or a paired set) that you can use for testing by doing things like:

* add fixed latency / jitter
* drop packets at a configurable rate
* optionally add/remove/translate VLAN tags

This is “driver-like” work because you’ll still implement a real `struct net_device`, `net_device_ops`, stats, queue control, and a proper RX path via NAPI. Linux net devices are registered (not `/dev` files), and are driven by callbacks and events.

### Core requirements (intermediate)

1. **Register a net_device**

* Allocate with `alloc_etherdev(sizeof(priv))`, set MAC, fill `netdev->netdev_ops`, then `register_netdev()`.

2. **Implement the essential netdev ops**

* `.ndo_open`, `.ndo_stop`, `.ndo_start_xmit`, `.ndo_get_stats64`, `.ndo_tx_timeout` (optional but good).

3. **TX path = enqueue to your own software “ring”**

* In `ndo_start_xmit(skb, dev)`, push `skb` into a lockless/locked ring or queue.
* If your ring is full, **stop the netdev queue** with `netif_stop_queue()`; wake it later when space frees.

4. **RX path uses NAPI (this is the key “real driver” part)**

* Add NAPI with `netif_napi_add()` and in your “interrupt-like” event, call `napi_schedule()` and “disable IRQs” (for a virtual driver you can emulate this with a flag).
* In your NAPI poll function, dequeue up to `budget` packets, and inject them into the stack with `netif_receive_skb()` / `napi_gro_receive()`. Complete with `napi_complete_done()` and “re-enable interrupts” when done.

5. **Make it a “utility”: impair / transform packets**
   Do impairments **between TX enqueue and RX delivery**, e.g.:

* delay: store `skb` with a target delivery time, release later via `hrtimer`/workqueue
* loss: probabilistic drop (update stats)
* VLAN rewrite: if `skb_vlan_tag_present(skb)` then rewrite `skb->vlan_tci`, or if VLAN header is in bytes, push/pull 802.1Q (this part becomes “harder” quickly)

### Acceptance tests (what “done” looks like)

* `ip link add`/module load creates `netimp0` (or `netimp0/netimp1` pair).
* `ip link set netimp0 up` works (your `.ndo_open` called; NAPI enabled; queue started).
* Put `netimp0` into a network namespace, assign IPs, and `ping` across your pair; observe delay/loss.
* `ip -s link show netimp0` shows RX/TX counters updating via `.ndo_get_stats64`.

---

## Stretch goals (harder)

* **Multiqueue** (2+ TX/RX rings, one NAPI per RX queue).
* **GRO support** (`napi_gro_receive()` instead of `netif_receive_skb()`).
* **XDP hook**: support attaching an XDP program (`.ndo_bpf`) and run it in your RX poll before building/passing an skb.

---

## Hard-mode alternative: write a real PCI NIC driver in QEMU (Intel e1000)

If you want “hard” in the traditional sense: implement a minimal driver for the QEMU-emulated **Intel e1000**:

* PCI probe for vendor/device IDs, map BAR0 MMIO, allocate `net_device`, set `netdev_ops`, register it.
* Implement descriptor rings, IRQ handler + NAPI, TX mapping, RX refill, link events, etc.

This matches a realistic flow: rings, DMA, interrupts, NAPI polling, and packet processing.

---

If you tell me which direction you want (**utility virtual NIC** vs **QEMU e1000 hardware-ish**), I’ll break it into a clean milestone checklist + suggested data structures (ring layout, locking model, where to schedule NAPI, stats, etc.).

Source you uploaded: 
