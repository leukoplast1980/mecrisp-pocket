#!/bin/sh
# Forth-level tests of the Mecrisp Pocket port, run through qpemu.
#
# Usage: tests/forth/run.sh QPEMU IMAGE WORKDIR SYMS
# SYMS is the core symbol table (core.syms from build-core.sh).
set -u

QPEMU=${1:?qpemu binary}
IMAGE=${2:?flash image}
WORK=${3:?work dir}
SYMS=${4:?core.syms}
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
    out=$(timeout 20 "$QPEMU" --flash "$FLASH" --syms "$SYMS" --timeout 3000 "$IMAGE" "$@" </dev/null 2>&1); rc=$?
    if [ $rc -eq 0 ] && matches_all "$out" "$want"; then
        pass=$((pass + 1)); echo "ok   $name"
    else
        fail=$((fail + 1)); echo "FAIL $name (exit $rc)"; printf '%s\n' "$out" | sed 's/^/     | /' | tail -15
    fi
}

# Like expect, but the output must NOT match.
reject() {
    name=$1; bad=$2; shift 2
    out=$(timeout 20 "$QPEMU" --flash "$FLASH" --syms "$SYMS" --timeout 3000 "$IMAGE" "$@" </dev/null 2>&1); rc=$?
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

# The C dictionary reader sees exactly the words Forth's own "words" lists.
dict_same() {
    name=$1; shift
    out=$(timeout 20 "$QPEMU" --flash "$FLASH" --syms "$SYMS" --timeout 3000 "$IMAGE" "$@" \
          -e words --words </dev/null 2>&1)
    forth=$(printf '%s\n' "$out" | grep "Name:" | grep -v "Flags: FFFFFFFF" | sed 's/.*Name: //' | sort)
    c=$(printf '%s\n' "$out" | grep "^word: " | cut -d' ' -f2 | sort)
    if [ -n "$c" ] && [ "$forth" = "$c" ]; then
        pass=$((pass + 1)); echo "ok   $name"
    else
        fail=$((fail + 1)); echo "FAIL $name"
        printf '%s\n' "$forth" > "$WORK/forth.txt"; printf '%s\n' "$c" > "$WORK/c.txt"
        diff "$WORK/forth.txt" "$WORK/c.txt" | sed 's/^/     | /' | head -10
    fi
}

dict_same "dictionary: core only"
dict_same "dictionary: RAM and flash" -e ": in-ram ;" -e "compiletoflash" -e ": in-flash ;" -e "compiletoram" -e ": in-ram-2 ;"
dict_same "dictionary: compiletoflash mode" -e ": in-ram-3 ;" -e "compiletoflash" -e ": in-flash-2 ;"
expect "clean up dictionary" "Finished"                      -e "eraseflash"
expect "where is the CPU"   "where: pc [0-9a-f]+ in spin"    -e ": spin begin again ;" -s "spin" --sleep 100 --where --break-after 10

# Loading a file line by line stops at the first error.
FILES=$(cd "$(dirname "$0")/files" && pwd)
expect "load a file"        "^7 sq \. 49  ok\.$"             -f "$FILES/good.fs"
expect "multi-line definition in a file" "^-5 clamp \. 9 clamp \. 0 9  ok\.$" -f "$FILES/good.fs"
out=$(timeout 20 "$QPEMU" --flash "$FLASH" --timeout 3000 "$IMAGE" -f "$FILES/bad.fs" </dev/null 2>&1); rc=$?
if [ $rc -eq 3 ] && printf '%s\n' "$out" | grep -q "bad.fs:5: error" && ! printf '%s\n' "$out" | grep -q ": c 3 ;"; then
    pass=$((pass + 1)); echo "ok   load stops at the first error"
else
    fail=$((fail + 1)); echo "FAIL load stops at the first error (exit $rc)"; printf '%s\n' "$out" | sed 's/^/     | /' | tail -10
fi

echo "forth tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
