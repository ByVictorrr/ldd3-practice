## Quick loop test (same guest)

You have both ends:

* **Device (gadget) side:** `/dev/ttyGS0`
* **Host (emulated) side:** `/dev/ttyACM0`

Open two shells:

**Shell A (read host side):**

```bash
stty -F /dev/ttyACM0 raw -echo -icrnl -onlcr
cat /dev/ttyACM0
```

**Shell B (write gadget side):**

```bash
stty -F /dev/ttyGS0 raw -echo -icrnl -onlcr
echo "hello from gadget" > /dev/ttyGS0
```

You should see the line appear in Shell A. Reverse directions to test both ways:

```bash
echo "hello from host" > /dev/ttyACM0
cat /dev/ttyGS0
```

(You can also use `socat` or `minicom` if you prefer.)

## Handy controls

* **Bind to a specific UDC (if needed):**

```bash
echo dummy_udc.0 > /sys/bus/usb_gadget/drivers/g_acm/bind
```

* **Unbind (free the UDC):**

```bash
echo dummy_udc.0 > /sys/bus/usb_gadget/drivers/g_acm/unbind
```

* **Unload the module:**

```bash
rmmod usb_g_acm
```

