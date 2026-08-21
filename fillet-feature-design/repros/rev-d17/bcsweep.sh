#!/bin/zsh
set -u
cd /Users/kerem/Devel/openscad/.claude/worktrees/rev-d17/work/ab
S=/private/tmp/claude-501/-Users-kerem-Devel-openscad/12d3d7aa-6891-4cbc-a906-1b8c466395c3/scratchpad
B=/Users/kerem/Devel/openscad/.claude/worktrees/rev-d17/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
H=/private/tmp/claude-501/-Users-kerem-Devel-openscad/a47a8762-5213-433e-9ed7-46aed0d0d6f2/scratchpad/wt-head/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
M=$1
OUT=$S/bcsweep_$2.txt
: > $OUT
for fn in $(seq 16 4 160); do
  env -u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER $H -o $S/rs_h.off -D FN=$fn $M >/dev/null 2>&1
  OPENSCAD_FILLET_LOCALGROUP=1 OPENSCAD_FILLET_SEAMOVER=0 $B -o $S/rs_l.off -D FN=$fn $M >/dev/null 2>&1
  OPENSCAD_FILLET_LOCALGROUP=1 $B -o $S/rs_f.off -D FN=$fn $M >/dev/null 2>&1
  h=$(python3 mesh2.py $S/rs_h.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  l=$(python3 mesh2.py $S/rs_l.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  f=$(python3 mesh2.py $S/rs_f.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  echo "fn=$fn head=$h l1=$l fix=$f" >> $OUT
done
echo DONE >> $OUT
