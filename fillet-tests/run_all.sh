#!/usr/bin/env bash
# Run every check of every variant of every case and compare each result against
# expectations.txt.
#
#   ./run_all.sh                          # everything
#   ./run_all.sh case_boss_base_fillet     # one case
#   OPENSCAD=/path/to/openscad ./run_all.sh
#
# Most real cases are expected to FAIL today: the operator is still being built,
# and a check that is red on purpose should read as "not there yet", not as
# breakage. expectations.txt records which. The script's own exit code is about
# SURPRISES only — a case that changed state without anyone updating the table.
# When a milestone lands, the expected-FAIL lines it fixes are what you delete.
# (Plain bash 3.2 compatible — no associative arrays.)

set -uo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
source "$ROOT/lib/_common.sh"
require_openscad

EXPECT="$ROOT/expectations.txt"

expected_for() {  # case variant check -> PASS|FAIL, default PASS
  local c="$1" v="$2" k="$3" ec ev ek exp trailing
  # The trailing field soaks up end-of-line comments so they stay out of `exp`.
  while read -r ec ev ek exp trailing; do
    [[ -z "${ec:-}" || "$ec" == \#* ]] && continue
    [[ "$ec" == "$c" || "$ec" == "*" ]] || continue
    [[ "$ev" == "$v" || "$ev" == "*" ]] || continue
    [[ "$ek" == "$k" || "$ek" == "*" ]] || continue
    echo "$exp"; return
  done < "$EXPECT"
  echo PASS
}

if [[ $# -gt 0 ]]; then files=("$@"); else files=("$ROOT"/cases/*.scad); fi

rc=0
total=0
surprises=0
for f in "${files[@]}"; do
  abs="$(case_path "$f")"
  [[ -z "$abs" ]] && { echo "skip (not found): $f" >&2; rc=1; continue; }
  case_name="$(basename "$abs" .scad)"

  while read -r tag idx name size has_ref tol; do
    [[ "$tag" == "VARIANT" ]] || continue
    for kind in $(checks_for_variant "$has_ref"); do
      out="$("$ROOT/check.sh" "$abs" "$name" "$kind" 2>&1)"
      case $? in 0) got=PASS ;; 1) got=FAIL ;; 3) got=CRASH ;; 4) got=TIMEOUT ;; *) got=ERROR ;; esac
      exp="$(expected_for "$case_name" "$name" "$kind")"
      total=$((total + 1))
      if [[ "$got" == "$exp" ]]; then
        printf "  ok       %-28s %-6s %-9s %s\n" "$case_name" "$name" "$kind" "$got"
      else
        printf "  SURPRISE %-28s %-6s %-9s got=%s expected=%s\n" \
               "$case_name" "$name" "$kind" "$got" "$exp"
        sed 's/^/           /' <<<"$out"
        surprises=$((surprises + 1))
        rc=1
      fi
    done
  done < <(probe_case "$abs")
done

echo
if [[ $surprises -eq 0 ]]; then
  echo "$total checks, all matched expectations"
else
  echo "$total checks, $surprises did not match expectations"
fi
exit $rc
