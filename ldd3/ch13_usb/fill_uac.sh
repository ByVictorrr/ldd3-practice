ffmpeg -hide_banner -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=5" \
       -f s16le -ac 2 - \
  | dd of=/dev/uac0 bs=192 status=progress