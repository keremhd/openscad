#!/usr/bin/env bash
# Render every case to a PNG in ./png/.
#
#   ./render.sh                              # all cases
#   ./render.sh case_inner_corner_fillet      # just one
#   OPENSCAD=/path/to/openscad ./render.sh    # override the binary
#   SIZE=1800,1200 ./render.sh                # bigger images
#   FAST=1 ./render.sh                        # skip the diff column (see below)
#
# Each image is a grid: one row per size variant, six columns per row —
#
#   [ model ] [ ref applied ] [ cand applied ] [ ref tool ] [ cand tool ] [ diff ]
#
# The diff column is the same geometry check.sh's "tool" check evaluates, so an
# empty last column and a green test are the same fact. It is also the expensive
# column (it dilates both solids), which is what FAST=1 turns off when you only
# want to eyeball the shapes.

set -uo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
source "$ROOT/lib/_common.sh"
require_openscad

OUT="$ROOT/png"
mkdir -p "$OUT"
SIZE=${SIZE:-1800,1200}

if [[ $# -gt 0 ]]; then files=("$@"); else files=("$ROOT"/cases/*.scad); fi

echo "Using: $BIN"
rc=0
for f in "${files[@]}"; do
  abs="$(case_path "$f")"
  [[ -z "$abs" ]] && { echo "skip (not found): $f" >&2; rc=1; continue; }
  name="$(basename "$abs" .scad)"

  # A vertical cut through a ring axis wants a front camera; everything else is
  # laid out to be read from the top.
  slice="$(probe_case "$abs" | sed -n 's/^SLICE //p')"
  if [[ "$slice" == "front" ]]; then rot="90,0,0"; else rot="0,0,0"; fi

  # The unsliced solid rows are for the GUI, where the view can be turned; in a
  # flat top-down image they would only hide the sections behind their outlines.
  args=(-D "FILLET_NO_SOLID=true")
  [[ -n "${FAST:-}" ]] && args+=(-D "FILLET_NO_DIFF=true")

  echo "rendering $name  (slice $slice, camera $rot)"
  run_bounded /dev/null "$BIN" "${BACKEND_ARGS[@]}" "${args[@]+"${args[@]}"}" \
    -o "$OUT/$name.png" --imgsize="$SIZE" --projection=o \
    --camera="0,0,0,$rot,600" --viewall --autocenter --render=1 "$abs"
  case $? in
    0) ;;
    124) echo "  TIMEOUT: $name (over ${CHECK_TIMEOUT}s — try FAST=1, which drops the diff column)" >&2; rc=1 ;;
    *)   echo "  FAILED:  $name" >&2; rc=1 ;;
  esac
done
echo "done -> $OUT/"
exit $rc
