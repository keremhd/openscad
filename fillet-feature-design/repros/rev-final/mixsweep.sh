#!/bin/zsh
# $fn sweep of the MIXED model: HEAD vs candidate (LOCALGROUP=1) vs candidate
# with the grouping forced global (GLOBALSUB=1), plus the served/group census
# from RDIAG on every row so no row can be inert and read as a win.
set -u
W=/Users/kerem/Devel/openscad/.claude/worktrees/rev-final
S=/private/tmp/claude-501/-Users-kerem-Devel-openscad/12d3d7aa-6891-4cbc-a906-1b8c466395c3/scratchpad/rf
B=$W/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
H=$S/headmine/OpenSCAD
M=$1; TAG=$2; MX=${3:-1}; LO=${4:-16}; HI=${5:-160}; ST=${6:-4}
OUT=$S/mix_$TAG.txt
: > $OUT
for fn in $(seq $LO $ST $HI); do
  env -u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER $H -o $S/mx_h_$TAG.off -D FN=$fn -D MIXED=$MX $M >/dev/null 2>&1
  env -u OPENSCAD_FILLET_SEAMOVER OPENSCAD_FILLET_LOCALGROUP=1 OPENSCAD_FILLET_RDIAG=1 $B -o $S/mx_f_$TAG.off -D FN=$fn -D MIXED=$MX $M 2>/dev/null > $S/mx_d_$TAG.txt
  env -u OPENSCAD_FILLET_SEAMOVER OPENSCAD_FILLET_LOCALGROUP=1 OPENSCAD_FILLET_GLOBALSUB=1 $B -o $S/mx_g_$TAG.off -D FN=$fn -D MIXED=$MX $M >/dev/null 2>&1
  h=$(python3 $W/work/ab/mesh2.py $S/mx_h_$TAG.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  f=$(python3 $W/work/ab/mesh2.py $S/mx_f_$TAG.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  g=$(python3 $W/work/ab/mesh2.py $S/mx_g_$TAG.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  cen=$(grep -h "RDIAG concave=1 " $S/mx_d_$TAG.txt | head -1 | sed 's/.*cand=/cand=/')
  grp=$(grep -h "RDIAG-GRP" $S/mx_d_$TAG.txt | head -1)
  if cmp -s $S/mx_h_$TAG.off $S/mx_f_$TAG.off; then id=IDENT; else id=DIFF; fi
  echo "fn=$fn head=$h fix=$f glob=$g $id | $cen | $grp" >> $OUT
  echo "fn=$fn head=$h fix=$f glob=$g $id | $cen | $grp"
done
echo DONE >> $OUT
