# 1) Virtual UDC
# sudo modprobe dummy_hcd is_super_speed=1   # creates a fake UDC and a matching host HCD
# sudo modprobe libcomposite

# 2) ConfigFS
# sudo mount -t configfs none /sys/kernel/config
cd /sys/kernel/config/usb_gadget
mkdir -p g1 && cd g1

# 3) IDs (example: Linux Foundation / ECM)
echo 0x1d6b > idVendor
echo 0x0104 > idProduct

# 4) Strings
mkdir -p strings/0x409
echo "Raspberry Pi USB Gadget" > strings/0x409/product
echo "0001"                     > strings/0x409/serialnumber
echo "Victor Pi"                > strings/0x409/manufacturer

# 5) Config & ECM function
mkdir -p configs/c.1
mkdir -p functions/ecm.usb0
ln -s functions/ecm.usb0 configs/c.1/

# 6) Bind to the virtual UDC (dummy_hcd)
ls /sys/class/udc/                # you'll see something like: dummy_udc.0
echo dummy_udc.0 > UDC            # use the exact name you saw above
