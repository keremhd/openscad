#!/usr/bin/env bash
# Run every eq_*.scad through check_equivalence.sh and summarise.
#
# Some cases are expected to FAIL until the operator milestone that implements
# them lands (see expected_result() below). This script reports each result
# against its expectation, so "red until M7" reads as expected, not as breakage.
# (Plain bash 3.2 compatible — no associative arrays.)

set -uo pipefail
cd "$(dirname "$0")"

# Expected result at the current milestone (M0).
expected_result() {
  case "$1" in
    eq_selftest_equal.scad)      echo PASS ;;  # harness sanity: equal shapes
    eq_selftest_wrongradius.scad) echo FAIL ;;  # harness sanity: catches wrong geometry
    eq_inner_corner.scad)        echo FAIL ;;  # real case: red until M7
    *)                           echo PASS ;;
  esac
}

rc=0
for f in eq_*.scad; do
  out="$(./check_equivalence.sh "$f" 2>&1)"; got_rc=$?
  case $got_rc in 0) got=PASS ;; 1) got=FAIL ;; *) got=ERROR ;; esac
  exp="$(expected_result "$f")"
  if [[ "$got" == "$exp" ]]; then
    printf "  ok     %-30s got=%s (expected)\n" "$f" "$got"
  else
    printf "  NOT OK %-30s got=%s expected=%s\n" "$f" "$got" "$exp"
    echo "$out" | sed 's/^/        /'
    rc=1
  fi
done
[[ $rc -eq 0 ]] && echo "all cases matched expectations" || echo "some cases did not match expectations"
exit $rc
