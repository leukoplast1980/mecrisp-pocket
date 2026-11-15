#!/bin/sh
# Forth-level tests of the Mecrisp Pocket port, run through qpemu.
#
# Usage: tests/forth/run.sh QPEMU IMAGE WORKDIR
set -u

QPEMU=${1:?qpemu binary}
IMAGE=${2:?flash image}
WORK=${3:?work dir}
mkdir -p "$WORK"
FLASH="$WORK/flash.img"
rm -f "$FLASH"

pass=0; fail=0

# expect NAME REGEXES [qpemu actions...]
# Passes if qpemu exits 0 and the output matches every regex (grep -E);
# separate several regexes with " && ".
matches_all() {
    rest=$2
    while :; do
        re=${rest%% && *}
        printf '%s\n' "$1" | grep -Eq -- "$re" || return 1
        [ "$re" = "$rest" ] && return 0
        rest=${rest#* && }
    done
}

expect() {
    name=$1; want=$2; shift 2
    out=$(timeout 20 "$QPEMU" --flash "$FLASH" --timeout 3000 "$IMAGE" "$@" </dev/null 2>&1); rc=$?
    if [ $rc -eq 0 ] && matches_all "$out" "$want"; then
        pass=$((pass + 1)); echo "ok   $name"
    else
        fail=$((fail + 1)); echo "FAIL $name (exit $rc)"; printf '%s\n' "$out" | sed 's/^/     | /' | tail -15
    fi
}

# Like expect, but the output must NOT match.
reject() {
    name=$1; bad=$2; shift 2
    out=$(timeout 20 "$QPEMU" --flash "$FLASH" --timeout 3000 "$IMAGE" "$@" </dev/null 2>&1); rc=$?
    if [ $rc -eq 0 ] && ! printf '%s\n' "$out" | grep -Eq -- "$bad"; then
        pass=$((pass + 1)); echo "ok   $name"
    else
        fail=$((fail + 1)); echo "FAIL $name (exit $rc)"; printf '%s\n' "$out" | sed 's/^/     | /' | tail -15
    fi
}

expect "boot banner"        "Mecrisp-Quintus .* on Mecrisp Pocket"
expect "arithmetic"         "^1 2 \+ \. 3  ok\.$"            -e "1 2 + ."
expect "multiply, divide"   "-42 7  ok\."                    -e "-6 7 * . 49 7 / ."
expect "hex input"          "255  ok\."                      -e '$FF .'
expect "fixed point"        "3,14"                           -e "3,14159 2 f.n"
expect "board id"           "51504B54  ok\."                 -e '$40040000 @ hex. '

# Flash survives a restart, then eraseflash removes it.
expect "compile to flash"   "ok\."                           -e "compiletoflash" -e ': hello ." Hello Pocket" ;' -e "compiletoram"
expect "flash after restart" "Hello Pocket"                  -e "hello"
expect "eraseflash"         "Finished\. Reset !"             -e "eraseflash"
expect "flash is empty"     "hello not found"                -e "hello"

# Core flash is write-protected.
expect "core protected"     "Hello Pocket"                   -e ': hello ." Hello Pocket" ;' -e '0 4 !' -e 'hello'

# Stop breaks an endless loop and returns to the prompt.
expect "stop endless loop"  "spin  Stop\. && ^1 2 \+ \. 3  ok\.$" -e ": spin begin again ;" -s "spin" --break-after 300 -e "1 2 + ."
expect "stack empty after stop" "Stack: \[ 0 \]"               -e ": count-up 0 begin 1+ again ;" -s "count-up" --break-after 200 -e ".s"

# A runaway data stack wrecks RAM, but cannot touch saved flash words.
expect "save a word"        "ok\."                           -e "compiletoflash" -e ': hello ." Hello Pocket" ;' -e "compiletoram"
expect "runaway stack"      "Exception"                      -e ": flood begin 1 again ;" -s "flood" --sleep 1500
expect "flash words survive" "Hello Pocket"                  -e "hello"
expect "clean up flash"     "Finished"                       -e "eraseflash"

# Exceptions are reported instead of looping forever.
expect "illegal address"    "Exception mcause: 00000001"     -e '$12345678 execute' -e "1 ."

# init runs on reset; safe boot skips it.
expect "init in flash"      "ok\."                           -e "compiletoflash" -e ': init ." INIT RAN" cr ;' -e "compiletoram"
expect "init runs on reset" "INIT RAN"                       --reset
expect "safe boot"          "Safe boot: init skipped"        --safe-reset
reject "safe boot skips init" "INIT RAN.*Safe boot|Safe boot(.|\n)*INIT RAN" --safe-reset
expect "software reset"     "Mecrisp-Quintus .* on Mecrisp Pocket" -e "reset"
expect "erase init"         "Finished"                       --safe-reset -e "eraseflash"

# A hanging init with interrupts off can only be fixed with safe boot.
expect "hanging init"       "ok\."                           -e "compiletoflash" -e ": init dint begin again ;" -e "compiletoram"
expect "safe boot rescues"  "Finished"                       --safe-reset -e "eraseflash"

echo "forth tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
