#!/bin/zsh
# Attack 3: rib.scad over $fn, HEAD vs gate-on (shipped) vs gate-deleted.
set -u
W=/Users/kerem/Devel/openscad/.claude/worktrees/rev-final
S=/private/tmp/claude-501/-Users-kerem-Devel-openscad/12d3d7aa-6891-4cbc-a906-1b8c466395c3/scratchpad/rf
B=$W/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
H=$S/headmine/OpenSCAD
M=$1; TAG=$2
OUT=$S/gate_$TAG.txt
: > $OUT
for fn in $(seq 16 4 160); do
  env -u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER $H -o $S/g_h_$TAG.off -D FN=$fn $M >/dev/null 2>&1
  env -u OPENSCAD_FILLET_SEAMOVER OPENSCAD_FILLET_LOCALGROUP=1 $B -o $S/g_on_$TAG.off -D FN=$fn $M >/dev/null 2>&1
  env -u OPENSCAD_FILLET_SEAMOVER OPENSCAD_FILLET_LOCALGROUP=1 OPENSCAD_FILLET_NOSTRAIGHT=1 OPENSCAD_FILLET_RDIAG=1 $B -o $S/g_off_$TAG.off -D FN=$fn $M 2>/dev/null > $S/g_d_$TAG.txt
  h=$(python3 $W/work/ab/mesh2.py $S/g_h_$TAG.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  a=$(python3 $W/work/ab/mesh2.py $S/g_on_$TAG.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  b=$(python3 $W/work/ab/mesh2.py $S/g_off_$TAG.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  cen=$(grep -h "RDIAG concave" $S/g_d_$TAG.txt | head -1 | sed 's/.*cand=/cand=/')
  grp=$(grep -h "RDIAG-GRP" $S/g_d_$TAG.txt | head -1)
  if cmp -s $S/g_h_$TAG.off $S/g_on_$TAG.off; then id=IDENT; else id=DIFF; fi
  echo "fn=$fn head=$h gateon=$a gateoff=$b $id | nogate: $cen $grp" >> $OUT
  echo "fn=$fn head=$h gateon=$a gateoff=$b $id"
done
echo DONE >> $OUT
