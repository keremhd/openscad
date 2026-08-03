#!/bin/zsh
# theta x r sweep of a model, HEAD vs candidate vs candidate+GLOBALSUB, with the
# served/group census printed on every row.
set -u
W=/Users/kerem/Devel/openscad/.claude/worktrees/rev-final
S=/private/tmp/claude-501/-Users-kerem-Devel-openscad/12d3d7aa-6891-4cbc-a906-1b8c466395c3/scratchpad/rf
B=$W/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
H=$S/headmine/OpenSCAD
M=$1; TAG=$2; MX=${3:-1}
OUT=$S/th_$TAG.txt
: > $OUT
for th in 60 75 90 105 120; do
 for rr in 1 2 3; do
  env -u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER $H -o $S/th_h_$TAG.off -D TH=$th -D RR=$rr -D MIXED=$MX $M >/dev/null 2>&1
  env -u OPENSCAD_FILLET_SEAMOVER OPENSCAD_FILLET_LOCALGROUP=1 OPENSCAD_FILLET_RDIAG=1 $B -o $S/th_f_$TAG.off -D TH=$th -D RR=$rr -D MIXED=$MX $M 2>/dev/null > $S/th_d_$TAG.txt
  env -u OPENSCAD_FILLET_SEAMOVER OPENSCAD_FILLET_LOCALGROUP=1 OPENSCAD_FILLET_GLOBALSUB=1 $B -o $S/th_g_$TAG.off -D TH=$th -D RR=$rr -D MIXED=$MX $M >/dev/null 2>&1
  h=$(python3 $W/work/ab/mesh2.py $S/th_h_$TAG.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  f=$(python3 $W/work/ab/mesh2.py $S/th_f_$TAG.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  g=$(python3 $W/work/ab/mesh2.py $S/th_g_$TAG.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  cen=$(grep -h "RDIAG concave" $S/th_d_$TAG.txt | head -1 | sed 's/.*cand=/cand=/')
  grp=$(grep -h "RDIAG-GRP" $S/th_d_$TAG.txt | head -1)
  if cmp -s $S/th_h_$TAG.off $S/th_f_$TAG.off; then id=IDENT; else id=DIFF; fi
  echo "th=$th r=$rr head=$h fix=$f glob=$g $id | $cen | $grp" >> $OUT
  echo "th=$th r=$rr head=$h fix=$f glob=$g $id | $cen | $grp"
 done
done
echo DONE >> $OUT
