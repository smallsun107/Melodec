#!/bin/sh
# Regenerate the fonts in this directory from the upstream releases.
#
# The fonts are NOT subset: everything the upstream faces contain is kept, so no
# script can be silently dropped. The only transformation is instancing the
# upstream *variable* fonts to a static Regular — without it stb_truetype would
# read the font's default master (which is Thin for Noto Sans SC/KR).
#
# Requires: fonttools (`python3 -m pip install fonttools`)
set -e
cd "$(dirname "$0")"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fetch() { curl -sL -o "$2" "$1"; }

echo "== Noto Sans SC =="
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf" "$WORK/sc-var.ttf"
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/OFL.txt" OFL-NotoSansSC.txt
fonttools varLib.instancer --update-name-table "$WORK/sc-var.ttf" wght=400 -o NotoSansSC-Regular.ttf

echo "== Noto Sans (Latin/Greek/Cyrillic/combining) =="
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf" "$WORK/sans-var.ttf"
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/notosans/OFL.txt" OFL-NotoSans.txt
fonttools varLib.instancer --update-name-table "$WORK/sans-var.ttf" wdth=100 wght=400 -o NotoSans-Regular.ttf

echo "== Noto Sans KR (Hangul) =="
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanskr/NotoSansKR%5Bwght%5D.ttf" "$WORK/kr-var.ttf"
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanskr/OFL.txt" OFL-NotoSansKR.txt
fonttools varLib.instancer --update-name-table "$WORK/kr-var.ttf" wght=400 -o NotoSansKR-Regular.ttf

ls -lh .
