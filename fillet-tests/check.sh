#!/usr/bin/env bash
# Run one check of one variant of one case, headlessly.
#
#   ./check.sh case_inner_corner_fillet small tool
#   ./check.sh case_boss_base_fillet            # every variant x every check
#
# Each check reduces to "is this solid empty?", which OpenSCAD reports on stderr
# and which is the one verdict that needs no baseline file. Two kinds expect
# empty, one expects non-empty:
#
#   tool      the candidate matches the hand-written reference within tolerance
#   sandwich  the candidate disturbs the model only within the tool's own size
#   emits     the candidate produced a tool solid at all
#
# Exit: 0 = PASS, 1 = FAIL, 2 = ERROR (binary missing, parse error, bad usage),
#       3 = CRASH (openscad died on a signal), 4 = TIMEOUT (over CHECK_TIMEOUT s).

set -uo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
source "$ROOT/lib/_common.sh"
require_openscad

CASE_ABS="$(case_path "${1:-}")"
[[ -z "$CASE_ABS" ]] && { echo "usage: $0 <case> [variant] [tool|sandwich|emits]" >&2; exit 2; }
WANT_VARIANT="${2:-}"
WANT_KIND="${3:-}"

run_one() {  # name size has_ref tol sign kind
  local name="$1" size="$2" tol="$4" sign="$5" kind="$6"
  local tmp out label="$(basename "$CASE_ABS" .scad) $name $kind"
  tmp="$(mktemp -d)"
  cat > "$tmp/driver.scad" <<EOF
include <$CASE_ABS>;
FILLET_DRIVER = true;
\$case_size = $size;
fillet_run_check("$kind", $tol, "$sign", $size) {
  case_model(); case_ref_tool(); case_cand_tool(); case_clip();
}
EOF
  local binrc
  run_bounded "$tmp/log" "$BIN" "${BACKEND_ARGS[@]}" -o "$tmp/out.stl" "$tmp/driver.scad"
  binrc=$?
  out="$(cat "$tmp/log")"
  rm -rf "$tmp"

  if [[ $binrc -eq 124 ]]; then
    echo "TIMEOUT $label  (over ${CHECK_TIMEOUT}s; raise CHECK_TIMEOUT to allow more)"
    return 4
  fi

  # A signal death is never a geometric verdict — report it as loudly as a parse
  # error, with the case that provoked it, rather than folding it into FAIL.
  if [[ $binrc -ge 128 ]]; then
    echo "CRASH $label  (openscad died with signal $((binrc - 128)))"
    return 3
  fi

  # A broken case file must not be misread as a geometric verdict.
  if grep -qiE "Parser error|syntax error|Can't open|Ignoring unknown|Assertion .* failed" <<<"$out"; then
    echo "ERROR $label"
    grep -iE "error|warning" <<<"$out" | head -3 | sed 's/^/    /'
    return 2
  fi

  # The dilation runs through CGAL's Nef kernel, which gives up on geometry that
  # is not a clean solid. That is a statement about the candidate, not about the
  # harness, so it counts as a failed check rather than a broken run.
  if grep -qi "CGAL error" <<<"$out"; then
    echo "FAIL  $label  (CGAL could not dilate: the candidate is not a clean solid)"
    return 1
  fi

  local empty=0
  grep -qi "top level object is empty" <<<"$out" && empty=1

  # "emits" is the one check whose pass condition is non-empty.
  local pass=0
  if [[ "$kind" == "emits" ]]; then
    [[ $empty -eq 0 ]] && pass=1
  else
    [[ $empty -eq 1 ]] && pass=1
  fi

  if [[ $pass -eq 1 ]]; then echo "PASS  $label"; return 0; fi
  case "$kind" in
    tool)     echo "FAIL  $label  (residual not empty: candidate differs from the reference by more than $tol)" ;;
    sandwich) echo "FAIL  $label  (result strays further than $size from the model)" ;;
    emits)    echo "FAIL  $label  (candidate tool is empty)" ;;
  esac
  return 1
}

rc=0
while read -r tag idx name size has_ref tol; do
  [[ "$tag" == "SIGN" ]] && { sign="$idx"; continue; }
  [[ "$tag" == "VARIANT" ]] || continue
  [[ -n "$WANT_VARIANT" && "$WANT_VARIANT" != "$name" ]] && continue
  for kind in $(checks_for_variant "$has_ref"); do
    [[ -n "$WANT_KIND" && "$WANT_KIND" != "$kind" ]] && continue
    run_one "$name" "$size" "$has_ref" "$tol" "$sign" "$kind"
    r=$?; [[ $r -gt $rc ]] && rc=$r
  done
done < <(probe_case "$CASE_ABS")

exit $rc
