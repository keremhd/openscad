#!/bin/zsh
set -u
cd /Users/kerem/Devel/openscad/.claude/worktrees/rev-d17/work/ab
S=/private/tmp/claude-501/-Users-kerem-Devel-openscad/12d3d7aa-6891-4cbc-a906-1b8c466395c3/scratchpad
B=/Users/kerem/Devel/openscad/.claude/worktrees/rev-d17/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
H=/private/tmp/claude-501/-Users-kerem-Devel-openscad/a47a8762-5213-433e-9ed7-46aed0d0d6f2/scratchpad/wt-head/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
TW=$1
OUT=$S/sw15_tw${TW}.txt
: > $OUT
for th in 60 75 90 105 120; do for r in 1 2 3; do
  env -u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER $H -o $S/o_h.off -D TH=$th -D RR=$r -D TW=$TW opocket.scad >/dev/null 2>&1
  OPENSCAD_FILLET_LOCALGROUP=1 $B -o $S/o_f.off -D TH=$th -D RR=$r -D TW=$TW opocket.scad >/dev/null 2>&1
  h=$(python3 mesh2.py $S/o_h.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  f=$(python3 mesh2.py $S/o_f.off | sed 's/.*nonman=\([0-9]*\).*/\1/')
  echo "th=$th r=$r head=$h fix=$f" >> $OUT
done; done
echo DONE >> $OUT
