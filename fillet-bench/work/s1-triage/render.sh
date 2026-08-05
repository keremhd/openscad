#!/bin/zsh
# Render the 21 not-valid cells of results/sweep-fd3dec78.tsv against a PINNED
# copy of the fd3dec78 binary, into work/s1-triage/stl/. One render per cell;
# the mesh counts are checked back against the sweep row, which is the
# known-answer control for this step.
set -u
cd "${0:A:h}/../.."          # fillet-bench
BIN=${BIN:?set BIN to the pinned binary}
OUT=work/s1-triage/stl
mkdir -p $OUT
while IFS=$'\t' read -r model fn r rest; do
  tag=${model}_fn${fn}_r${r}
  [[ -s $OUT/$tag.stl ]] && { print "skip $tag"; continue; }
  args=()
  [[ $fn == def ]] || args+=(-D FNSET=$fn)
  [[ $r  == def ]] || args+=(-D R=$r)
  $BIN --enable=fillet --backend=manifold --render $args -o $OUT/$tag.stl \
       --export-format asciistl models/$model.scad > $OUT/$tag.log 2>&1
  print "done $tag rc=$? $(wc -l < $OUT/$tag.stl 2>/dev/null)"
done < <(awk -F'\t' 'NR>1 && $4!="VALID"' results/sweep-fd3dec78.tsv)
