#!/bin/zsh
# Bundle the contact-sheet PNGs into a single multi-page PDF.
#
#   ./sheets-to-pdf.sh                 sheets/sheet-*.png -> sheets/sheets.pdf
#   ./sheets-to-pdf.sh out.pdf         same, custom output path
#
# One sheet PNG per page, in natural sheet order. Uses ImageMagick.
set -eu
cd "${0:A:h}"

OUT=${1:-sheets/sheets.pdf}
pages=(sheets/sheet-*.png(n))   # (n) = numeric sort, so sheet-2 precedes sheet-10
[[ ${#pages[@]} -gt 0 ]] || { print -u2 "no sheets/sheet-*.png -- run ./sheet.sh first"; exit 1; }

if (( $+commands[magick] )); then
  magick "${pages[@]}" "$OUT"
elif (( $+commands[convert] )); then
  convert "${pages[@]}" "$OUT"
else
  print -u2 "need ImageMagick (magick or convert) on PATH"; exit 1
fi

print "wrote $OUT  (${#pages[@]} pages: ${pages##*/})"
