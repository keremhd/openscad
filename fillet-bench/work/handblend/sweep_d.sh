#!/bin/zsh
# d sweep, one row per cell, appended as it completes.
BIN=/private/tmp/claude-501/-Users-kerem-Devel-openscad/86517c18-0145-4bf3-879b-972815bd1c7a/scratchpad/pin-roundtool-gate/OpenSCAD.app/Contents/MacOS/OpenSCAD
WD=/Users/kerem/Devel/openscad/fillet-bench/work/handblend
cd $WD
OUT=$1; shift
BRUSHV=$1; shift
KV=$1; shift
MA=$1; shift
for D in "$@"; do
  TAG="${OUT}_D${D}"
  LOG=out/$TAG.log
  mkdir -p out
  $BIN --enable=fillet --backend=manifold -D "D=$D" -D "BRUSH=$BRUSHV" -D "KSET=$KV" -D "MINANG=$MA" \
       -o out/$TAG.stl handblend_step.scad > $LOG 2>&1
  RC=$?
  H=$(python3 -c "print(1.0+$D)")
  W=$(grep -c "WARNING" $LOG)
  MSG=$(grep "WARNING" $LOG | head -3 | tr '\n' ' | ')
  # box around the test edge, away from the z end caps
  P=$(python3 probe.py out/$TAG.stl --box -1.2 0.2 $(python3 -c "print($H-1.2)") $(python3 -c "print($H+0.2)") 3 17)
  print -r -- "D=$D rc=$RC warns=$W $P" >> $OUT.rows
  print -r -- "D=$D rc=$RC warns=$W $P"
  [[ -n "$MSG" ]] && { print -r -- "    WARN: $MSG" >> $OUT.rows; print -r -- "    WARN: $MSG" }
done
