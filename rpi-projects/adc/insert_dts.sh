dtc -@ -I dts -O dtb -o mcp3008-tutorial.dtbo mcp3008-tutorial.dts
sudo dtoverlay -v ./mcp3008-tutorial.dtbo
sudo cp  ./mcp3008-tutorial.dtbo /boot/overlays