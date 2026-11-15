#!/bin/sh
# Builds the flash image shipped with the app.
#
# Usage: scripts/build-image.sh OUTDIR
# Produces OUTDIR/pocket.img (256 KiB flash image) and OUTDIR/pocket.json
# (image hash and the core symbol addresses the app needs).
set -eu

OUT=${1:?usage: build-image.sh OUTDIR}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK="$OUT/.core"

"$HERE/build-core.sh" "$WORK" >/dev/null
cp "$WORK/core.bin" "$OUT/pocket.img"

if command -v sha256sum >/dev/null 2>&1; then
    sha=$(sha256sum "$OUT/pocket.img" | cut -d' ' -f1)
else
    sha=$(shasum -a 256 "$OUT/pocket.img" | cut -d' ' -f1)
fi

sym() {
    addr=$(awk -v name="$1" '$3 == name { print $1 }' "$WORK/core.syms")
    [ -n "$addr" ] || { echo "symbol $1 missing" >&2; exit 1; }
    printf '    "%s": "0x%s"' "$1" "$addr"
}

{
    echo "{"
    echo "  \"imageSha256\": \"$sha\","
    echo "  \"quintusVersion\": \"1.1.1d\","
    echo "  \"symbols\": {"
    sym CoreDictionaryAnfang; echo ","
    sym Dictionarypointer; echo ","
    sym Fadenende; echo ","
    sym ZweitFadenende; echo ","
    sym FlashDictionaryAnfang; echo ","
    sym RamDictionaryAnfang; echo ","
    sym datenstackanfang; echo ","
    sym returnstackanfang; echo ","
    sym quit; echo
    echo "  }"
    echo "}"
} > "$OUT/pocket.json"

rm -rf "$WORK"
echo "pocket.img: $sha"
