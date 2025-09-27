set auto-load safe-path /
set target-async off
set non-stop off
set pagination off
set confirm off
set remotetimeout 20

add-auto-load-safe-path /home/victord/kernels/linux-stable/scripts/gdb
python import sys,importlib; sys.path.insert(0,'/home/victord/kernels/linux-stable/scripts/gdb'); sys.modules.pop('linux',None); importlib.invalidate_caches()
source /home/victord/kernels/linux-stable/scripts/gdb/vmlinux-gdb.py


# handle SIGTRAP noprint pass nostop
# handle SIGTRAP noprint nostop pass
# continue