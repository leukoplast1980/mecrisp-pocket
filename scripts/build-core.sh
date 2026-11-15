#!/bin/sh
# Assemble the Mecrisp-Quintus core for the Mecrisp Pocket board.
#
# Usage: scripts/build-core.sh OUTDIR
# Produces OUTDIR/core.elf, OUTDIR/core.bin (256 KiB flash image, core only),
# OUTDIR/core.list (disassembly) and OUTDIR/core.syms (symbol table).
#
# Needs GNU binutils for RISC-V: Debian/Ubuntu package binutils-riscv64-linux-gnu.
set -eu

OUT=${1:?usage: build-core.sh OUTDIR}
CROSS=${CROSS:-riscv64-linux-gnu}
HERE=$(cd "$(dirname "$0")/.." && pwd)
SRC="$HERE/quintus/pocket"

mkdir -p "$OUT"
"$CROSS-as" --warn --fatal-warnings -march=rv32im -I "$SRC" \
    "$SRC/mecrisp-quintus-pocket.s" -o "$OUT/core.o"
"$CROSS-ld" -o "$OUT/core.elf" -T "$SRC/memmap" "$OUT/core.o" -m elf32lriscv
"$CROSS-objdump" -Mnumeric -D "$OUT/core.elf" > "$OUT/core.list"
"$CROSS-objcopy" "$OUT/core.elf" "$OUT/core.bin" -O binary
"$CROSS-nm" "$OUT/core.elf" | sort > "$OUT/core.syms"

size=$(wc -c < "$OUT/core.bin")
if [ "$size" -ne 262144 ]; then
    echo "core.bin is $size bytes, expected 262144" >&2
    exit 1
fi
echo "core.bin: $size bytes"
