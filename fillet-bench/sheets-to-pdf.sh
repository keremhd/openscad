#!/bin/zsh
# Bundle the contact-sheet PNGs into a single multi-page A4 PDF.
#
#   ./sheets-to-pdf.sh                 sheets/sheet-*.png -> sheets/sheets.pdf
#   ./sheets-to-pdf.sh out.pdf         same, custom output path
#
# One sheet PNG per A4 page (portrait), scaled to fit with the aspect ratio
# preserved and centred on the page, in natural sheet order.
#
# macOS toolchain: sips (Apple, wraps each PNG in a PDF) + Ghostscript
# (forces every page to A4 and fits the content). ImageMagick's own PDF writer
# mangles the MediaBox for these tall contact sheets, so it is not used here.
set -eu
cd "${0:A:h}"

OUT=${1:-sheets/sheets.pdf}
pages=(sheets/sheet-*.png(n))   # (n) = numeric sort, so sheet-2 precedes sheet-10
[[ ${#pages[@]} -gt 0 ]] || { print -u2 "no sheets/sheet-*.png -- run ./sheet.sh first"; exit 1; }
(( $+commands[sips] )) || { print -u2 "need sips (macOS)"; exit 1; }
(( $+commands[gs] ))   || { print -u2 "need Ghostscript (gs) on PATH"; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

parts=()
for p in "${pages[@]}"; do
  part="$tmp/${p:t:r}.pdf"
  sips -s format pdf "$p" --out "$part" >/dev/null
  parts+=("$part")
done

# -dFIXEDMEDIA + -sPAPERSIZE=a4 pin every output page to A4; -dPDFFitPage scales
# each input page to fit, preserving aspect and centring it.
gs -sDEVICE=pdfwrite -dBATCH -dNOPAUSE -q \
   -sPAPERSIZE=a4 -dFIXEDMEDIA -dPDFFitPage \
   -o "$OUT" "${parts[@]}"

print "wrote $OUT  (${#pages[@]} A4 pages: ${pages##*/})"
