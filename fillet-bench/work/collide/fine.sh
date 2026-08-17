#!/bin/zsh
# Finer steps either side of each model's exact collision equality (where the
# first sweep showed the mesh collapsing to a fraction of its vertex count), plus
# a PNG of every case worth looking at.
set -u
cd "${0:A:h}"
export LC_ALL=C LC_NUMERIC=C

BIN=/Users/kerem/Devel/openscad/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
BENCH=/Users/kerem/Devel/openscad/fillet-bench
PY=/private/tmp/claude-501/-Users-kerem-Devel-openscad/84d9bee9-8cfb-41d1-8df7-9006c106d19e/scratchpad/venv/bin/python
CENSUS=/private/tmp/claude-501/-Users-kerem-Devel-openscad/84d9bee9-8cfb-41d1-8df7-9006c106d19e/scratchpad/foldcensus.py

mkdir -p out png
print "model\tr\tcorners\trefused\twarn\tmesh\tfolds"

fine=(
  "c1_thin_wall|9/10 99/100 101/100 11/10"
  "c2_parallel_creases|19/10 199/100 201/100 21/10"
  "c3_thin_web|149/100 151/100 11/4 29/10"
  "c4_endface_eaten|24/10 26/10 27/10 28/10"
  "c5_narrow_slot|149/100 151/100 16/10"
  "c6_over_round_cube|29/10 31/10 45/10 6"
)

for entry in $fine; do
  name=${entry%%|*}
  for r in ${=entry#*|}; do
    tag=$name-$(print $r | tr '/' '_')
    stl=out/$tag.stl; log=out/$tag.log
    rm -f $stl
    $BIN --enable=fillet --backend=manifold --render --export-format asciistl \
        -D RSET=$r -o $stl $name.scad > $log 2>&1
    corners=$(sed -n 's/^ECHO: fillet: corners \(.*\) in file.*/\1/p' $log | sort -u | paste -sd'/' -)
    corners=${corners:-none}
    warn=$(grep -c "WARNING:" $log); warn=${warn:-0}
    if grep -q "returned unchanged" $log; then refused=REFUSED; else refused=built; fi
    if [[ -f $stl ]]; then
      mesh=$(python3 $BENCH/mesh.py --tol 1e-6 $stl 2>&1 | sed "s|.*$tag.stl: ||")
      folds=$($PY $CENSUS $stl 2>&1 | head -1 | cut -f2)
    else
      mesh="NO MESH"; folds="-"
    fi
    print "$name\t$r\t$corners\t$refused\t$warn\t$mesh\t$folds"
  done
done

# Pictures of the worst cells: the collapses, the fold-heavy cells, the refusal
# and the two over-rounded extremes.
shots=(
  "c1_thin_wall|1" "c1_thin_wall|2"
  "c2_parallel_creases|2" "c2_parallel_creases|7/2"
  "c3_thin_web|3/2" "c3_thin_web|3"
  "c4_endface_eaten|5/2" "c4_endface_eaten|11/4"
  "c5_narrow_slot|3/2" "c5_narrow_slot|5/2"
  "c6_over_round_cube|3" "c6_over_round_cube|4"
)
for s in $shots; do
  name=${s%%|*}; r=${s#*|}
  tag=$name-$(print $r | tr '/' '_')
  $BIN --enable=fillet --backend=manifold --render --viewall --autocenter \
      --imgsize=700,560 --projection=p --colorscheme=Cornfield \
      -D RSET=$r -o png/$tag.png $name.scad >/dev/null 2>&1
  print "png/$tag.png"
done
