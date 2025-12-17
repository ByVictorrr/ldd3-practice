# Putting It All Together: A More Realistic RAM Disk Block Driver (`rbull`, Linux 6.x + blk-mq)

This section summarizes the steps to write a **minimal-but-not-toy** block driver for a RAM disk in Linux 6.x using **blk-mq**, then test it in **QEMU**.

The goal is a `/dev/rbull0` disk that:

* supports reads/writes via blk-mq (`queue_rq`)
* can be partitioned (MBR/GPT) and formatted (ext4, vfat, etc.)
* shows up correctly in `lsblk`, `fdisk`, `/proc/partitions`, etc.

---

## 0) Decide the design choices up front (small but important)

Before code, decide these so your driver’s behavior is consistent:

* **Sector size**: usually 512 bytes (you can allow a module param, but keep it simple at first).
* **Disk capacity**: module param like `disk_mb=64` or a fixed number of sectors.
* **Queue model**: blk-mq (modern). Start with:

  * `nr_hw_queues = 1`
  * `queue_depth = 64` or 128
* **Ops supported**:

  * Required: READ/WRITE
  * Optional: FLUSH, DISCARD (TRIM-like), WRITE_ZEROES
* **Concurrency**:

  * For a RAM disk, you *can* allow parallel requests.
  * If you want deterministic behavior for overlapping writes, add a `spinlock` around the copy loop (simpler, slower, but predictable).

---

## 1) Define device parameters (module params + constants)

Typical parameters you’ll want:

* `disk_mb` (capacity in MB)
* `logical_block_size` (default 512)
* `queue_depth`
* `nr_hw_queues` (keep 1 initially)
* `nminors` (allow partitions: e.g. 16 minors is common)

Example (conceptually):

* `disk_mb=128` → capacity = 128 * 1024 * 1024 bytes
* `capacity_sectors = capacity_bytes / logical_block_size`

---

## 2) Define the device structure (what you must track)

A “slightly more complete” `struct rbull_dev` usually contains:

* **Backing store**

  * `u8 *data;`  (vmalloc’d buffer)
  * `u64 size_bytes;`
  * `sector_t capacity_sectors;`

* **blk-mq objects**

  * `struct blk_mq_tag_set tag_set;`
  * `struct gendisk *gd;`  (includes `queue` pointer)

* **Bookkeeping**

  * `int major;`
  * `spinlock_t lock;` (optional, for write serialization)
  * `atomic_t open_count;` (optional, useful for debug)

* **Naming**

  * `char disk_name[DISK_NAME_LEN];`

Why this matters: once you start adding features (discard, flush, multiple minors), having these fields early avoids redesign later.

---

## 3) Implement request handling: `queue_rq` (blk-mq core)

This is where your driver actually performs I/O.

### What `queue_rq` must do (internally)

A “realistic” `queue_rq` flow looks like this:

1. Extract the request from `struct blk_mq_queue_data *bd`
2. Call `blk_mq_start_request(rq)` (tells the block layer you accepted it)
3. Validate operation:

  * Reject passthrough requests (`blk_rq_is_passthrough(rq)`)
  * Support only what you implement (READ/WRITE at minimum)
4. Compute disk offset:

  * `start_sector = blk_rq_pos(rq)`
  * `total_bytes = blk_rq_bytes(rq)`
  * `offset_bytes = start_sector * logical_block_size`
5. Bounds + alignment checks:

  * `offset_bytes + total_bytes <= dev->size_bytes`
  * For simplicity, require `total_bytes` multiple of logical block size (many drivers do)
6. Iterate the request’s memory segments:

  * Use `rq_for_each_segment(bvec, rq, iter)`
  * Map pages (`kmap_local_page`), copy data using `memcpy`
  * Update `offset_bytes` as you go
7. End the request with status:

  * `blk_mq_end_request(rq, BLK_STS_OK)` or an error status

### Optional additions that make it “more complex” (and more realistic)

* **REQ_OP_FLUSH**:

  * For RAM disk, it’s effectively a no-op → return OK.
* **REQ_OP_DISCARD / WRITE_ZEROES**:

  * Implement by `memset()` on the affected range.
* **Statistics / tracing**:

  * Add counters for reads/writes/bytes.
  * Avoid `printk` in fast path; it will destroy performance.

---

## 4) Implement `block_device_operations` (mostly trivial for RAM disk)

For a RAM disk, these are usually simple but still matter:

* `.owner = THIS_MODULE`
* `.open` / `.release`:

  * track open count (optional)
* `.ioctl`:

  * often unnecessary unless you want special controls
* `.getgeo`:

  * legacy; rarely needed now, but some tools still query it

Most of the time you can keep ops minimal; the block layer does heavy lifting.

---

## 5) Module init: `rbull_init()` (a more complete ordering)

Here’s the enhanced init outline with realistic error handling:

### A) Register a major number

* `major = register_blkdev(0, "rbull");`
* Store it for cleanup.

### B) Allocate backing memory

* `dev->data = vmalloc(size_bytes);`
* `memset(dev->data, 0, size_bytes);`
* `vmalloc` is common for larger sizes because it doesn’t require physically contiguous memory.

### C) Initialize the tag set (blk-mq)

Configure:

* `tag_set.ops = &rbull_mq_ops;`
* `tag_set.nr_hw_queues = 1;`
* `tag_set.queue_depth = queue_depth;`
* `tag_set.numa_node = NUMA_NO_NODE;`
* `tag_set.cmd_size = 0;` (you can later store per-request private data here)
* `tag_set.flags = BLK_MQ_F_SHOULD_MERGE;` (optional)

Then:

* `ret = blk_mq_alloc_tag_set(&dev->tag_set);`

### D) Allocate disk + queue (modern approach)

Preferred in newer kernels:

* `dev->gd = blk_mq_alloc_disk(&dev->tag_set, dev);`
* Check `IS_ERR(dev->gd)`.

### E) Configure `gendisk`

Set:

* `dev->gd->major = major;`
* `dev->gd->first_minor = 0;`
* `dev->gd->minors = nminors;`  ✅ enables partitions like `/dev/rbull0p1`
* `dev->gd->fops = &rbull_fops;`
* `dev->gd->private_data = dev;`
* `snprintf(dev->gd->disk_name, …, "rbull0");`
* `set_capacity(dev->gd, capacity_sectors);`

### F) Configure queue limits / hints (more realistic)

These hints improve behavior and tool output:

* `blk_queue_logical_block_size(dev->gd->queue, 512);`
* `blk_queue_physical_block_size(dev->gd->queue, 512);` (optional)
* Mark non-rotational:

  * `blk_queue_flag_set(QUEUE_FLAG_NONROT, dev->gd->queue);`
* Optional: enable discard range rules if you implement discard:

  * set discard granularity and max discard sectors (varies by kernel APIs)

### G) Register the disk

* `add_disk(dev->gd);`

At this point you should see `/dev/rbull0`.

---

## 6) Module exit: `rbull_exit()` (cleanup ordering + safety)

A typical clean teardown flow:

1. Remove disk from system:

  * `del_gendisk(dev->gd);`
2. Cleanup disk/queue resources:

  * Depending on kernel, you may do `blk_cleanup_disk(dev->gd);`

    * (Some kernels expect `put_disk()` patterns; check your exact 6.x minor API expectations.)
3. Free tag set:

  * `blk_mq_free_tag_set(&dev->tag_set);`
4. Free memory:

  * `vfree(dev->data);`
5. Unregister major:

  * `unregister_blkdev(major, "rbull");`

### Error paths (recommended style)

Use `goto` unwinding so partial init failures don’t leak:

* `goto out_free_tagset;`
* `goto out_vfree;`
* `goto out_unregister;`

This is one of the main “complexity upgrades” that makes drivers maintainable.

---

# Testing it in QEMU (more complete plan)

You can test either:

* by compiling the module *inside* a guest distro, or
* by baking it into an initramfs

Below is a practical approach that doesn’t require a full custom OS build.

## Option A: Test inside a Linux distro guest (fastest mentally)

1. Boot a distro in QEMU (any that has kernel headers + build tools)
2. Copy your module source into the VM (shared folder or scp)
3. Build module against guest kernel headers
4. Insert module and test

### In the guest: insert + verify device

```sh
sudo insmod rbull.ko disk_mb=128 queue_depth=128
dmesg | tail -n 50
lsblk
cat /proc/partitions
```

You should see `rbull0`.

### Partition it (MBR example)

```sh
sudo fdisk /dev/rbull0
# create partition 1, write table
sudo partprobe /dev/rbull0   # or: blockdev --rereadpt /dev/rbull0
lsblk
```

Now you should see `/dev/rbull0p1`.

### Format + mount

```sh
sudo mkfs.ext4 /dev/rbull0p1
sudo mkdir -p /mnt/rbull
sudo mount /dev/rbull0p1 /mnt/rbull
df -h /mnt/rbull
```

### Data integrity test

```sh
dd if=/dev/urandom of=/mnt/rbull/blob bs=1M count=32 conv=fdatasync
sha256sum /mnt/rbull/blob
sync
sha256sum /mnt/rbull/blob
```

If hashes match across multiple reads, your read/write path is working.

---

## Option B: Boot kernel + initramfs + module (more “driver-dev like”)

This is closer to how kernel folks test minimal systems:

* Build kernel + initramfs with BusyBox
* Include your `.ko` module in the initramfs
* Boot with `console=ttyS0` and run everything in serial output

Useful when you want full control and no distro noise.

---

# Small “complexity upgrades” you can add next (if you want)

If you want to push it beyond basic:

1. **Support DISCARD**

  * makes `blkdiscard /dev/rbull0` work
2. **Support READ_ONLY mode**

  * module param `ro=1`; set disk read-only
3. **Multiple disks**

  * allocate `N` ramdisks → `/dev/rbull0`, `/dev/rbull1`, …
4. **Alignment-aware performance**

  * enforce 4K alignment and advertise it via queue limits
5. **Partition rescans**

  * after writing a partition table from userspace, ensure `BLKRRPART` / `partprobe` causes partition reread cleanly

---

## If you prefer `volblk` instead of `rbull`

Make these substitutions:

* `"rbull"` → `"volblk"` (for `register_blkdev` and `unregister_blkdev`)
* `"rbull0"` → `"volblk0"` (for `gd->disk_name`)
* `rbull.ko` → `volblk.ko`
* `rbull_*` symbols → `volblk_*` symbols
* `/dev/rbull0` → `/dev/volblk0`
* mount directory `/mnt/rbull` → `/mnt/volblk`

Partitions will show as:

* `/dev/volblk0p1`, `/dev/volblk0p2`, …

---

If you tell me which one you’re committing to (**rbull** or **volblk**), I can also rewrite your *code symbol naming scheme* cleanly (struct names, file names, Kconfig/Makefile naming) so the project looks polished end-to-end.
