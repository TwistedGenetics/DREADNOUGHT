#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# tools/preflight.py against host-built ELFs that use the real firmware/app.ld: a good layout must pass,
# and a start-up copy that runs past its section (the Carnac OS 2026-10-15 bug) must fail.
# Needs a host gcc that can build 32-bit (gcc-multilib); skipped otherwise.
set -e
cd "$(dirname "$0")/.."
T=build/host/preflight
rm -rf "$T"; mkdir -p "$T"
cat > "$T/t.c" <<'EOF'
int dat[64] = {1, 2, 3, 4, 5};
int bss[256];
__attribute__((section(".pool"))) int pool[1024];
__attribute__((section(".ram_text"))) int rt[32] = {7, 7, 7};
__attribute__((section(".c1_text"))) int c1[16] = {9, 9};
const int ro[8] = {3};
void _start(void) {}
EOF
if ! gcc -m32 -c -O0 -fno-pic -ffreestanding "$T/t.c" -o "$T/t.o" 2>/dev/null; then
    echo "preflight test: no 32-bit host gcc, skipped"; exit 0
fi
sed 's/KEEP(\*(.text.crt0)) //' firmware/app.ld > "$T/good.ld"
sed 's/_rt_end = _c1_end;/_rt_end = _c1_end + 0x10000;/' "$T/good.ld" > "$T/bad.ld"
mk() { mkdir -p "$T/$1"; ld -m elf_i386 -T "$T/$1.ld" "$T/t.o" -o "$T/$1/felucca.elf" --no-warn-rwx-segments 2>/dev/null ||
       ld -m elf_i386 -T "$T/$1.ld" "$T/t.o" -o "$T/$1/felucca.elf"; objcopy -O binary "$T/$1/felucca.elf" "$T/$1/felucca.bin"; }
mk good; mk bad
PREFLIGHT_OBJDUMP=objdump python3 tools/preflight.py "$T/good" || { echo "preflight test: a good layout failed"; exit 1; }
if PREFLIGHT_OBJDUMP=objdump python3 tools/preflight.py "$T/bad" >/dev/null; then
    echo "preflight test: an overrunning start-up copy passed"; exit 1
fi
echo "preflight test: good layout passes, overrun caught"
