dtc -@ -I dts -O dtb -o pcm5102a-tutorial.dtbo pcm5102a-tutorial.dts
sudo dtoverlay -v ./pcm5102a-tutorial.dtbo
sudo cp  ./pcm5102a-tutorial.dtbo /boot/overlays