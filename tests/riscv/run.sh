#!/bin/sh
# Builds and runs the official riscv-tests ISA suites rv32ui, rv32um and
# rv32mi ("p" environment) on the Mecrisp Pocket CPU core.
#
# Usage: tests/riscv/run.sh RISCV_TESTS_DIR ISA_RUNNER OUTDIR
# Needs gcc-riscv64-linux-gnu. Tests listed in skip.txt are not run.
set -u

RT=${1:?riscv-tests checkout}
RUNNER=${2:?isa-runner binary}
OUT=${3:?output dir}
CC=${CC:-riscv64-linux-gnu-gcc}
HERE=$(cd "$(dirname "$0")" && pwd)

mkdir -p "$OUT"
pass=0; fail=0; failed=""

for suite in rv32ui rv32um rv32mi; do
    for src in "$RT/isa/$suite"/*.S; do
        name="$suite-$(basename "$src" .S)"
        if grep -qx "$name" "$HERE/skip.txt" 2>/dev/null; then
            continue
        fi
        if ! "$CC" -march=rv32im_zicsr_zifencei -mabi=ilp32 -static -mcmodel=medany \
                -fvisibility=hidden -nostdlib -nostartfiles \
                -I"$RT/env/p" -I"$RT/isa/macros/scalar" -T"$RT/env/p/link.ld" \
                "$src" -o "$OUT/$name" 2>"$OUT/$name.log"; then
            echo "BUILD FAIL $name (see $OUT/$name.log)"
            fail=$((fail + 1)); failed="$failed $name"
            continue
        fi
        if "$RUNNER" "$OUT/$name" >/dev/null 2>"$OUT/$name.log"; then
            pass=$((pass + 1))
        else
            echo "FAIL $name: $(cat "$OUT/$name.log")"
            fail=$((fail + 1)); failed="$failed $name"
        fi
    done
done

echo "riscv-tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
