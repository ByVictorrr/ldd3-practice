**Mon Nov 3, 2025 — America/Los_Angeles**

awesome—let’s do an “end-of-chapter” capstone that makes the **Linux device model** real: you’ll register a **custom bus**, a **device** on that bus, a **driver** that matches & probes it, plus a **class** so udev makes `/dev` nodes and sysfs looks familiar. This directly exercises the chapter’s ideas: **kobjects, buses, devices, drivers, classes, sysfs, uevents, probe/remove, release()**. I’ll give you a drop-in skeleton you can build with a kernel tree or headers.

---

# Capstone: “mintbus” — a tiny end-to-end Linux device-model stack

## What you’ll learn/verify (mapped to the chapter)

* **kobject under the hood** (refcount, release) via `struct device`’s lifetime.
* **Bus/Device/Driver** roles, `match()`/`probe()`/`remove()`, and sysfs layout under `/sys/bus/<name>` and `/sys/devices`.
* **Driver binding flow** (bus `match` ⇒ driver `probe`, unbind on unload).
* **Class devices** and the `/sys/class/<name>` view + uevent → `/dev` node creation.

---

## Repo layout

```
mintbus/
├─ Kbuild + Makefile (out-of-tree)
├─ mintbus.h
├─ mintbus_bus.c        # defines struct bus_type + match/uevent
├─ mintbus_device.c     # registers one “mint0” device on the bus
├─ mintbus_driver.c     # a driver that binds to “mint0”
└─ mintbus_class.c      # optional: class + class device to surface /dev/mint0
```

---

## 1) The Bus (`mintbus_bus.c`)

Registers a **bus_type** named `"mintbus"`, implements a **name-based match**, and adds a tiny **uevent** var so you can see it in `udevadm monitor`. Sysfs appears at `/sys/bus/mintbus/{devices,drivers}`.

```c
// mintbus_bus.c
#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>

static int mintbus_match(struct device *dev, struct device_driver *drv) {
    /* simple: driver->name must be a prefix of dev_name(dev) */
    return strncmp(dev_name(dev), drv->name, strlen(drv->name)) == 0;
}

static int mintbus_uevent(struct device *dev, struct kobj_uevent_env *env) {
    return add_uevent_var(env, "MINT_NAME=%s", dev_name(dev));
}

struct bus_type mintbus_bus = {
    .name   = "mintbus",
    .match  = mintbus_match,     // decides driver<->device compatibility
    .uevent = mintbus_uevent,    // adds env vars for userspace rules
};
EXPORT_SYMBOL_GPL(mintbus_bus);

static int __init mintbus_init(void) { return bus_register(&mintbus_bus); }
static void __exit mintbus_exit(void) { bus_unregister(&mintbus_bus); }

module_init(mintbus_init);
module_exit(mintbus_exit);
MODULE_LICENSE("GPL");
```

> Why this matters: **`match()`** is the heart of binding; **`uevent()`** lets the bus add env vars udev can use.

---

## 2) A Device on the Bus (`mintbus_device.c`)

We create a **virtual device** (parentless), set `.bus = &mintbus_bus`, and (crucial!) a **`release()`** so the core can free it when the refcount hits zero. Its sysfs will live under `/sys/devices/virtual/...` and link into the bus and class as appropriate.

```c
// mintbus_device.c
#include <linux/device.h>
#include <linux/module.h>

extern struct bus_type mintbus_bus;
static struct device mint0;

static void mint_release(struct device *dev) {
    pr_info("mintbus: release %s\n", dev_name(dev));
}

static int __init mintdev_init(void) {
    device_initialize(&mint0);                    // init dev + kobject
    mint0.bus = &mintbus_bus;                     // attach to our bus
    mint0.release = mint_release;                 // required!
    dev_set_name(&mint0, "mint0");                // device name
    return device_add(&mint0);                    // adds to sysfs, emits uevent
}

static void __exit mintdev_exit(void) {
    device_del(&mint0);
    put_device(&mint0);                           // balances device_initialize
}
module_init(mintdev_init);
module_exit(mintdev_exit);
MODULE_LICENSE("GPL");
```

> Note the **release()** requirement—device memory is freed only after last put; forgetting it triggers warnings.

---

## 3) The Driver (`mintbus_driver.c`)

A small **device_driver** whose `.name = "mint"` matches devices named `"mint*"` via the bus `match()`. It creates a **device attribute** you can read/write in sysfs (`/sys/devices/.../flavor`).

```c
// mintbus_driver.c
#include <linux/device.h>
#include <linux/module.h>

extern struct bus_type mintbus_bus;
static ssize_t flavor_show(struct device *d, struct device_attribute *a, char *b){
    return sysfs_emit(b, "spearmint\n");
}
static ssize_t flavor_store(struct device *d, struct device_attribute *a,
                            const char *buf, size_t len){ return len; }
static DEVICE_ATTR_RW(flavor);

static int mint_probe(struct device *dev){
    pr_info("mintbus: probe %s\n", dev_name(dev));
    return device_create_file(dev, &dev_attr_flavor);
}
static int mint_remove(struct device *dev){
    device_remove_file(dev, &dev_attr_flavor);
    pr_info("mintbus: remove %s\n", dev_name(dev));
    return 0;
}

static struct device_driver mint_drv = {
    .name   = "mint",         // matches "mint0" via bus's match()
    .bus    = &mintbus_bus,
    .probe  = mint_probe,
    .remove = mint_remove,
    .owner  = THIS_MODULE,
};

static int __init mintdrv_init(void){ return driver_register(&mint_drv); }
static void __exit mintdrv_exit(void){ driver_unregister(&mint_drv); }
module_init(mintdrv_init);
module_exit(mintdrv_exit);
MODULE_LICENSE("GPL");
```

---

## 4) Optional: A Class that creates `/dev/mint0` (`mintbus_class.c`)

Give userspace a familiar **/dev** node and a `/sys/class/mint/` view. When the device is added, we create a **class device** (`device_create`) so udev makes `/dev/mint0` automatically.

```c
// mintbus_class.c
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/module.h>

static struct class *mint_class;
static dev_t mint_devno;
static struct cdev mint_cdev;

static int __init mintclass_init(void){
    int ret = alloc_chrdev_region(&mint_devno, 0, 1, "mint");
    if (ret) return ret;
    cdev_init(&mint_cdev, &(struct file_operations){});
    cdev_add(&mint_cdev, mint_devno, 1);

    mint_class = class_create(THIS_MODULE, "mint");
    if (IS_ERR(mint_class)) return PTR_ERR(mint_class);

    /* parent=NULL => shows under /sys/devices/virtual/mint/mint0 and
       creates /sys/class/mint/mint0 symlink; uevent -> /dev/mint0 */
    device_create(mint_class, NULL, mint_devno, NULL, "mint0");
    return 0;
}
static void __exit mintclass_exit(void){
    device_destroy(mint_class, mint_devno);
    class_destroy(mint_class);
    cdev_del(&mint_cdev);
    unregister_chrdev_region(mint_devno, 1);
}
module_init(mintclass_init);
module_exit(mintclass_exit);
MODULE_LICENSE("GPL");
```

> This follows the “modern char device pattern” so sysfs and `/dev` line up cleanly (class + device_create).

---

## 5) Build & run

**Out-of-tree Makefile (top-level):**

```make
obj-m += mintbus_bus.o mintbus_device.o mintbus_driver.o mintbus_class.o

KDIR ?= /lib/modules/$(shell uname -r)/build
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

**Load order (four terminals or one by one):**

```bash
sudo insmod mintbus_bus.ko
sudo insmod mintbus_driver.ko
sudo insmod mintbus_device.ko
sudo insmod mintbus_class.ko
```

**What you should see:**

* `/sys/bus/mintbus/` exists; `devices/mint0` link and `drivers/mint/` with a `mint0` symlink inside.
* The real device path under `/sys/devices/virtual/.../mint0`, with `subsystem` and `driver` symlinks, `uevent`, `power/`, and your `flavor` attribute.
* `/sys/class/mint/mint0` symlink to that device, and `/dev/mint0` created by udev (watch with `udevadm monitor`).

**Try it:**

```bash
cat /sys/class/mint/mint0/device/flavor
echo peppermint | sudo tee /sys/class/mint/mint0/device/flavor
```

**Unload (observe remove paths/uevents):**

```bash
sudo rmmod mintbus_class
sudo rmmod mintbus_device
sudo rmmod mintbus_driver
sudo rmmod mintbus_bus
```

---

## 6) Stretch goals (pick any)

* **Hotplug experiment:** Add a second virtual device (`mint1`) at runtime with another module; watch `probe()` and sysfs links update.
* **Attribute hygiene:** Convert `flavor` into a **read-only** attribute under the driver vs. device and compare where it lands in sysfs.
* **Userspace introspection:** From `/dev/mint0` implement trivial `read`/`write` in `file_operations` and confirm major:minor in `/sys/dev/char/`. (That’s the **kobject in `struct cdev`** showing up.)
* **Uevent rule:** Create a udev rule matching `MINT_NAME==mint0` to `SYMLINK+="mint-special"`, proving the bus `uevent()` var works.
* **Power path sanity:** Inspect `/sys/class/mint/mint0/device/power/` and read `control` to see runtime PM files auto-present (courtesy of the core).

---

## 7) How this mirrors the chapter (at a glance)

* **Kobjects as foundation** → you never allocate raw kobjects here; you use `device_*`/`driver_*`/`bus_*` helpers that wrap them.
* **One object, many views** → device appears under `/sys/devices`, bus links under `/sys/bus/mintbus`, class link under `/sys/class/mint`, all pointing to the same underlying device object.
* **Binding lifecycle** → register driver, core matches against existing devices, calls `probe`; remove does the inverse, tearing down sysfs entries.

---

If you want, I can wrap this into a single tarball/zip with the Makefile and module sources exactly as shown so you can `make` + `insmod` immediately.
