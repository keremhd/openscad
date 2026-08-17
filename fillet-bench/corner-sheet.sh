#!/bin/zsh
# Render the corner-dispatch page: one zoomed tile per corner-blend dispatch
# class, aimed at the junction vertex that class is about.
#
#   ./corner-sheet.sh              the whole page
#   ./corner-sheet.sh S5-T02       just that tile, re-rendered in place
#   ./corner-sheet.sh --no-pdf     skip the PDF bundle (sheet.sh does it once)
#
# Sheets 1-4 are whole models at stock defaults: they answer "is the mesh
# valid". This page answers "what does the corner look like", which no viewall
# render of a 40 mm bracket can show. Every tile is the same stock-defaults
# render as its whole-model tile, only with the camera parked on one vertex.
#
# The dispatch class in each caption is MEASURED. fillet() now echoes a second
# line per call --
#   fillet: corners tube=N capTri=N cap=N coons=N saddle=N flat=N fan=N weld=N none=N
# -- so every label below is read off that line from a stock-defaults render,
# and the line itself is carried into INDEX.md beside the label. The counters
# are per CALL, not per corner, so a model that mixes classes is captioned with
# the mix rather than with the one class its tile happens to aim at. A label
# that stops matching the measured column is the finding.
#
# Five of these labels were expectations that measurement contradicted: tee and
# tee_oblique close on the flat ear-clip, not a loft membrane; cross has no mixed
# corner at all, only single-sign spherical caps; mixed_fn falls to the centroid
# fan; refused_neighbour's "loft:partial" is the saddle field. Three more were
# incomplete rather than wrong -- rib is half saddle, pocket half cap, and
# shallow_crease has no fold corner at stock at all, because at stock the fold is
# below min_angle and is not selected.
#
# Tile ids continue sheet.sh's numbering: this is sheet 5, so S5-T01..S5-T12.
# Rows are checkpointed to INDEX.md as each tile completes.
set -u
cd "${0:A:h}"
zmodload zsh/mathfunc   # sqrt(), for turning the eye direction into a unit vector

# The camera is the one place in the bench that computes a decimal number and
# hands it back as text. Under a comma-decimal locale printf writes "36,616",
# which --camera reads as two more fields and the render silently produces no
# file. Pin the numeric locale for the whole script, printf and OpenSCAD alike.
export LC_ALL=C LC_NUMERIC=C

BIN=${BIN:-../build/OpenSCAD.app/Contents/MacOS/OpenSCAD}
FLAGS=${FLAGS:---enable=fillet}   # the modules are experimental; without this every model is an unknown-module error
TILE_W=${TILE_W:-700}
TILE_H=${TILE_H:-560}
COLS=3
ROWS=4
SHEET=5
FONT=/System/Library/Fonts/Helvetica.ttc

[[ -x $BIN ]] || { print -u2 "no binary at $BIN -- set BIN="; exit 1; }
print "binary: $BIN"
print "        built $(stat -f '%Sm' "$BIN")"
print "sources newer than the binary:"
find ../src -name '*.cc' -newer "$BIN" -o -name '*.h' -newer "$BIN" | head -5
print ""

mkdir -p tiles sheets
INDEX=sheets/INDEX.md
ONLY=""
NOPDF=""
for a in "$@"; do
  case $a in
    --no-pdf) NOPDF=1 ;;
    *) ONLY=$a ;;
  esac
done

# model | corner | dispatch | centre x,y,z | eye direction x,y,z | eye distance
#
# The centre is a vertex read off the model's own source, not a guess: lbracket
# is cube(40,30,6) + cube(6,30,40), so its reflex crease runs x=6 z=6 from y=0
# to y=30 and its two elbows are exactly (6,0,6) and (6,30,6). The direction is
# a unit-ish vector from the corner towards the eye; the distance sets the zoom,
# roughly 0.4 * distance millimetres of frame height at the default 22.5 degree
# perspective field of view.
tiles=(
  "lbracket|front elbow (6,0,6)|torus tube=2|6,0,6|1,-1,0.7|55"
  "lbracket|back elbow (6,30,6) mirrored|torus tube=2|6,30,6|1,1,0.7|55"
  "box_step|front elbow (24,0,10)|torus tube=2|24,0,10|1,-1,0.7|60"
  "box_step|back elbow (24,40,10) mirrored|torus tube=2|24,40,10|1,1,0.7|60"
  "rib|rib end on plate (20,2.5,2.5)|torus + saddle tube=4 saddle=4|20,2.5,2.5|1,1,0.8|32"
  "pocket|pocket floor corner (12,12,-2)|torus + tri-cap tube=4 capTri=12|12,12,-2|-1,-1,2.2|42"
  "shallow_crease|fold end face, ridge x=60 (60,0,16..36)|stock tube=0; min_angle 25 tube=2|60,0,26|0.4,-1,0.45|140"
  "tee|cylinder junction (5,0,10)|flat ear-clip flat=2|4,0,10.5|0.5,-1,0.5|44"
  "cross|three-axis star (0,0,0)|tri-cap valleys capTri=8, flat=3|0,0,0|1,-1,1|58"
  "tee_oblique|oblique junction (5,0,11.5)|flat ear-clip flat=4|4,0,11.5|0.5,-1,0.5|44"
  "refused_neighbour|refused crease (7.5,4,7.44) + built bead|saddle saddle=6|7.5,0,7.45|1,-0.6,0.8|40"
  "mixed_fn|48/10-facet junction (6,0,10)|centroid fan fan=4|5,0,10.5|0.5,-1,0.5|50"
)

print "${#tiles[@]} corner tiles -> sheets/sheet-$SHEET.png\n"

# A standalone full run owns its own rows and NOTHING ELSE in the index: cut out
# exactly this page's own section -- from our "## " heading to the next one --
# and stash whatever followed it, so re-running never doubles our table and never
# eats a later page's. The stash goes back on the end when we finish, or when we
# are interrupted, so a run killed by the stall watchdog still leaves the other
# sections intact. Order between the sheet scripts is therefore irrelevant.
HEADING="## Corner dispatch"
if [[ -z $ONLY ]]; then
  if [[ -f $INDEX ]]; then
    : > $INDEX.head; : > $INDEX.tail
    awk -v h="$HEADING" -v head="$INDEX.head" -v tail="$INDEX.tail" '
      state == 0 && index($0, h) == 1 { state = 1 }
      state == 1 && /^## / && index($0, h) != 1 { state = 2 }
      state == 0 { print > head; next }
      state == 2 { print > tail }
    ' $INDEX
    # Trailing blank lines go, and the stash is put back behind one blank line of
    # our own: without this the separator blank doubles on every single run and
    # the file grows a line at a time forever.
    perl -0pi -e 's/\n+\z/\n/' $INDEX.head
    mv $INDEX.head $INDEX
    trap 'if [[ -s '$INDEX'.tail ]]; then print "" >> '$INDEX'; cat '$INDEX'.tail >> '$INDEX'; fi; rm -f '$INDEX'.tail' EXIT INT TERM
  fi
  print "\n## Corner dispatch (sheet $SHEET)\n" >> $INDEX
  print "Generated by \`corner-sheet.sh\`. Same stock-defaults render as the model's" >> $INDEX
  print "whole-model tile, camera parked on one vertex. The dispatch column is a" >> $INDEX
  print "label read off the counters in the next column, which are measured: they" >> $INDEX
  print "are the \`fillet: corners ...\` echo of this very render. The counters are" >> $INDEX
  print "per call, not per corner, so a model that mixes classes is captioned with" >> $INDEX
  print "the mix. A label that stops matching its counters is the finding.\n" >> $INDEX
  print "| tile | model | corner | dispatch | corners (measured) | camera (eye -> centre) | warnings |" >> $INDEX
  print "|---|---|---|---|---|---|---|" >> $INDEX
fi

i=0
for entry in $tiles; do
  i=$((i + 1))
  id=$(printf "S%d-T%02d" $SHEET $i)
  [[ -n $ONLY && $ONLY != $id ]] && continue

  name=${entry%%|*};       rest=${entry#*|}
  corner=${rest%%|*};      rest=${rest#*|}
  dispatch=${rest%%|*};    rest=${rest#*|}
  centre=${rest%%|*};      rest=${rest#*|}
  dir=${rest%%|*};         dist=${rest#*|}

  cx=${centre%%,*}; cr=${centre#*,}; cy=${cr%%,*}; cz=${cr#*,}
  dx=${dir%%,*};    dr=${dir#*,};    dy=${dr%%,*}; dz=${dr#*,}
  len=$(( sqrt(dx * dx + dy * dy + dz * dz) ))
  ex=$(printf '%.3f' $(( cx + dx / len * dist )))
  ey=$(printf '%.3f' $(( cy + dy / len * dist )))
  ez=$(printf '%.3f' $(( cz + dz / len * dist )))
  cam="$ex,$ey,$ez,$cx,$cy,$cz"

  png=tiles/$id.png
  log=tiles/$id.log

  # No --viewall/--autocenter here: they would undo the aim. Everything else
  # matches sheet.sh, so a corner tile and its whole-model tile are the same
  # render seen from two places.
  ${=BIN} $=FLAGS --backend=manifold --render \
      --imgsize=$TILE_W,$TILE_H --projection=p --colorscheme=Cornfield \
      --camera=$cam -o $png models/$name.scad > $log 2>&1

  warn=$(grep -c "WARNING:" $log 2>/dev/null); warn=${warn:-0}

  # The dispatch echo of THIS render, so the label above is checkable against
  # the run that produced the picture rather than against a memory of one. One
  # line per fillet() call; shallow_crease makes two, joined with " / ".
  corners=$(sed -n 's/^ECHO: fillet: corners \(.*\) in file.*/\1/p' $log | paste -sd'/' - | sed 's|/| / |g')
  corners=${corners:-none echoed}

  if [[ -f $png ]]; then
    magick $png -background white -bordercolor white -border 6 \
      -font $FONT -pointsize 22 -fill black \
      label:"$id  $name  [$dispatch]" -gravity center -append \
      -font $FONT -pointsize 18 -fill $([[ $warn == 0 ]] && print gray30 || print red) \
      label:"$corner   warnings=$warn" -gravity center -append \
      tiles/$id-labeled.png 2>/dev/null
  else
    print -u2 "$id: NO OUTPUT -- see $log"
  fi

  print "$id $name $corner [$dispatch] $corners cam=$cam warnings=$warn"
  [[ -z $ONLY ]] && print "| $id | $name | $corner | $dispatch | \`$corners\` | \`$cam\` | $warn |" >> $INDEX
done

files=(tiles/S$SHEET-T*-labeled.png(N))
if (( ${#files} )); then
  montage -background white -tile ${COLS}x${ROWS} -geometry +10+10 \
      $files sheets/sheet-$SHEET.png 2>/dev/null
  magick sheets/sheet-$SHEET.png -background white -font $FONT -pointsize 34 -fill black \
      label:"fillet bench -- sheet $SHEET: corner dispatch (labels measured from the dispatch echo)" \
      -gravity center +swap -append sheets/sheet-$SHEET.png 2>/dev/null
  print "sheets/sheet-$SHEET.png"
fi

if [[ -z $NOPDF && -x ./sheets-to-pdf.sh ]]; then
  ./sheets-to-pdf.sh || print -u2 "sheets-to-pdf.sh failed -- PNGs are still current"
fi
