
# 1) One-liner “reload” (idempotent)

```bash
# scripts/reload.sh
#!/usr/bin/env bash
set -euo pipefail
modname="${1:?usage: $0 <module-name> [ko-path] }"
ko="${2:-$(dirname "$0")/../ldd3/$modname/$modname.ko}"

if lsmod | awk '{print $1}' | grep -qx "$modname"; then
  echo "[i] $modname already loaded -> rmmod"
  sudo rmmod "$modname"
fi

echo "[+] insmod $ko"
sudo insmod "$ko"
dmesg | tail -n 60
```

One button to (re)load, no errors if it’s already present.

# 2) “Build + reload” wrapper

```bash
# scripts/kbuild.sh
#!/usr/bin/env bash
set -euo pipefail
moddir="${1:?usage: $0 <relative-module-dir> (e.g., ldd3/ch02_hello)}"
KDIR="${KDIR:-/lib/modules/$(uname -r)/build}"
( cd "$moddir" && make -j"$(nproc)" KDIR="$KDIR" )
```

Now you can do:

```bash
./scripts/kbuild.sh ldd3/ch02_hello && ./scripts/reload.sh hello
```

# 3) Safer rmmod with users report

```bash
# scripts/rmmod_safe.sh
#!/usr/bin/env bash
set -euo pipefail
mod="${1:?usage: $0 <module-name>}"
if ! lsmod | awk '{print $1}' | grep -qx "$mod"; then
  echo "[i] $mod not loaded"; exit 0
fi
echo "[i] users of $mod:"
grep -E "^$mod " /proc/modules || true
sudo rmmod "$mod" && dmesg | tail -n 30
```

# 4) Quick dynamic-debug toggles (no rebuilds)

```bash
# scripts/ddbg-on.sh
#!/usr/bin/env bash
set -euo pipefail
mod="${1:?usage: $0 <module-name>}"
sudo sh -c "echo 'module $mod +p' > /sys/kernel/debug/dynamic_debug/control"
echo "[+] pr_debug enabled for $mod"

# scripts/ddbg-off.sh
#!/usr/bin/env bash
set -euo pipefail
mod="${1:?usage: $0 <module-name>}"
sudo sh -c "echo 'module $mod -p' > /sys/kernel/debug/dynamic_debug/control"
echo "[-] pr_debug disabled for $mod"
```

# 5) ftrace on your module (call-graph)

```bash
# scripts/trace-on.sh
#!/usr/bin/env bash
set -euo pipefail
mod="${1:?usage: $0 <module-name>}"
echo function_graph | sudo tee /sys/kernel/debug/tracing/current_tracer >/dev/null
echo ":mod:$mod" | sudo tee /sys/kernel/debug/tracing/set_ftrace_filter >/dev/null
sudo sh -c 'echo 1 > /sys/kernel/debug/tracing/tracing_on'
sudo tail -f /sys/kernel/debug/tracing/trace_pipe

# scripts/trace-off.sh
#!/usr/bin/env bash
set -euo pipefail
sudo sh -c 'echo 0 > /sys/kernel/debug/tracing/tracing_on'
```

# 6) Module info + sections (handy for GDB)

```bash
# scripts/mod-sections.sh
#!/usr/bin/env bash
set -euo pipefail
mod="${1:?usage: $0 <module-name>}"
modinfo "$mod" || true
echo "[sections]"
for s in .text .data .bss; do
  printf "%-6s %s\n" "$s" "$(cat /sys/module/$mod/sections/$s 2>/dev/null || echo '-')"
done
```

# 7) CLion External Tools (parameterized)

Create these once and reuse for every folder:

**Build (current file’s folder)**

* Program: `/usr/bin/ssh`
* Arguments:

```
-t <user>@<host> 'cd ~/clion/<project>/$FileDirRelativeToProjectRoot$ && KDIR=${KDIR:-/lib/modules/$(uname -r)/build} make -j$(nproc)'
```

**Reload (derive module from folder name)**

* Program: `/usr/bin/ssh`
* Arguments:

```
-t <user>@<host> 'cd ~/clion/<project>/$FileDirRelativeToProjectRoot$ && mod=${PWD##*/}; [[ -f $mod.ko ]] || { echo "no $mod.ko"; exit 1; }; ../../scripts/reload.sh "$mod" "./$mod.ko"'
```

**pr\_debug on/off**

* Program: `/usr/bin/ssh`
* Arguments (on):

```
-t <user>@<host> 'cd ~/clion/<project>/$FileDirRelativeToProjectRoot$ && mod=${PWD##*/}; ../../scripts/ddbg-on.sh "$mod"'
```

* Arguments (off): same but `ddbg-off.sh`.

**ftrace follow**

* Program: `/usr/bin/ssh`
* Arguments:

```
-t <user>@<host> 'cd ~/clion/<project>/$FileDirRelativeToProjectRoot$ && mod=${PWD##*/}; ../../scripts/trace-on.sh "$mod"'
```

Now you don’t type module names; it infers from the folder.

# 8) Makefile quality-of-life (per module)

```make
# ldd3/chXX_*/Makefile
-include ../../toolchain/kbuild.mk
obj-m += $(notdir $(CURDIR)).o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

This auto-names the target `.o` from the directory (`ch02_hello -> ch02_hello.o`). If you prefer distinct `.c` names, keep your explicit `obj-m += hello.o`.

# 9) Top-level helpers

**.editorconfig** (consistent tabs/LF)

```
root = true
[*]
end_of_line = lf
insert_final_newline = true
charset = utf-8
[*.{c,h,sh,make,mak,Makefile}]
indent_style = space
indent_size = 2
```

**.gitattributes** (kill CRLF)

```
* text=auto eol=lf
```

**Pre-commit sanity (optional)**

```bash
# scripts/pre-commit.sh
#!/usr/bin/env bash
set -e
command -v shellcheck >/dev/null && shellcheck scripts/*.sh || true
grep -R --line-number $'\r' . && { echo "CRLF detected"; exit 1; } || true
```

Then: `git config hooks.path scripts && ln -s ../scripts/pre-commit.sh .git/hooks/pre-commit`

# 10) Headers/kernels switcher

```bash
# scripts/set-kdir.sh
#!/usr/bin/env bash
set -euo pipefail
kdir="${1:?usage: $0 </path/to/kernel/build>}"
export KDIR="$kdir"
echo "KDIR=$KDIR exported. Use: KDIR=$KDIR make"
```

Use when you test against multiple kernels.

# 11) “Dev shell” for quick labs

```bash
# scripts/devsh.sh
#!/usr/bin/env bash
set -euo pipefail
cd "${1:-ldd3/ch02_hello}"
echo "[i] entering $PWD on remote with root dmesg"
exec ssh -t <user>@<host> "cd ~/clion/<project>/$PWD && bash -l"
```

# 12) Tiny README template per lab

Create `ldd3/chXX_*/README.md` and copy:

```markdown
## Goal
What this lab shows (e.g., module init/exit, pr_debug, dynamic_debug).

## Build & Run
```

KDIR=/lib/modules/\$(uname -r)/build make
../../scripts/reload.sh <mod>

```

## Notes
- Gotchas you hit, fixes, links.
```

---

