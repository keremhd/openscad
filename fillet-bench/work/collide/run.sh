#!/bin/zsh
# Sweep each collision probe across r through its collision threshold.
# Radii are written as fractions: a decimal point in a -D argument is read by
# atof, which returns 0 under a comma-decimal locale.
set -u
cd "${0:A:h}"
export LC_ALL=C LC_NUMERIC=C

BIN=/Users/kerem/Devel/openscad/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
BENCH=/Users/kerem/Devel/openscad/fillet-bench
PY=/private/tmp/claude-501/-Users-kerem-Devel-openscad/84d9bee9-8cfb-41d1-8df7-9006c106d19e/scratchpad/venv/bin/python
CENSUS=/private/tmp/claude-501/-Users-kerem-Devel-openscad/84d9bee9-8cfb-41d1-8df7-9006c106d19e/scratchpad/foldcensus.py

mkdir -p out
print "model\tr\tcorners\tblend\trefused\twarn\tmesh\tfolds"

cases=(
  "c1_thin_wall|1/2 3/4 1 5/4 3/2 2"
  "c2_parallel_creases|1 3/2 2 5/2 3 7/2"
  "c3_thin_web|1 5/4 3/2 2 5/2 3"
  "c4_endface_eaten|1 2 5/2 11/4 3"
  "c5_narrow_slot|1/2 1 5/4 3/2 2 5/2"
  "c6_over_round_cube|1 2 5/2 3 7/2 4"
)

for entry in $cases; do
  name=${entry%%|*}
  for r in ${=entry#*|}; do
    tag=$name-$(print $r | tr '/' '_')
    stl=out/$tag.stl
    log=out/$tag.log
    rm -f $stl
    $BIN --enable=fillet --backend=manifold --render --export-format asciistl \
        -D RSET=$r -o $stl $name.scad > $log 2>&1

    corners=$(sed -n 's/^ECHO: fillet: corners \(.*\) in file.*/\1/p' $log | sort -u | paste -sd'/' -)
    corners=${corners:-none}
    blend=$(sed -n 's/^ECHO: fillet: r [^ ]* blended \(.*\) in file.*/\1/p' $log | sort -u | paste -sd'/' -)
    blend=${blend:-none}
    warn=$(grep -c "WARNING:" $log); warn=${warn:-0}
    if grep -q "returned unchanged" $log; then refused=REFUSED; else refused=built; fi
    if [[ -f $stl ]]; then
      mesh=$(python3 $BENCH/mesh.py --tol 1e-6 $stl 2>&1 | sed "s|.*$tag.stl: ||")
      folds=$($PY $CENSUS $stl 2>&1 | head -1 | cut -f2)
    else
      mesh="NO MESH"; folds="-"
    fi
    print "$name\t$r\t$corners\t$blend\t$refused\t$warn\t$mesh\t$folds"
  done
done
