#!/bin/zsh
# The second axis: the round_tool radius against a fixed hand-built blend.
# Ball seating predicts the flip at RT* = D + R*tan(DELTA/2), i.e. the extent of
# the flat wall, and says nothing about the blend's own radius.
BIN=/private/tmp/claude-501/-Users-kerem-Devel-openscad/86517c18-0145-4bf3-879b-972815bd1c7a/scratchpad/pin-roundtool-gate/OpenSCAD.app/Contents/MacOS/OpenSCAD
cd /Users/kerem/Devel/openscad/fillet-bench/work/handblend
OUT=$1; shift; DV=$1; shift; MA=$1; shift
mkdir -p out
for RT in "$@"; do
  TAG="${OUT}_RT${RT}"; LOG=out/$TAG.log
  $BIN --enable=fillet --backend=manifold -D "D=$DV" -D "RT=$RT" -D 'BRUSH=true' -D "MINANG=$MA" \
       -o out/$TAG.stl handblend_step.scad > $LOG 2>&1
  H=$(python3 -c "print(1.0+$DV)")
  P=$(python3 probe.py out/$TAG.stl --box -1.3 1.3 -0.3 $(python3 -c "print($H+0.3)") 6 14)
  W=$(grep "WARNING" $LOG | sed 's/.*round_tool/round_tool/;s/ in file.*//' | head -1)
  print -r -- "RT=$RT $P"
  [[ -n "$W" ]] && print -r -- "     $W"
done
