#!/usr/bin/env bash
# Render every case_*.scad in this folder to a PNG.
#
# Usage:
#   ./render.sh                # render all cases into ./png/
#   ./render.sh case_inner_corner.scad case_outer_edge.scad   # only these
#   OPENSCAD=/path/to/openscad ./render.sh    # override the binary
#
# Camera: each case is built so its section lies in a fixed plane. Files that
# take a vertical (through-axis) section are rendered from the front; the rest
# top-down. See README.md.

set -euo pipefail
cd "$(dirname "$0")"

# --- locate the OpenSCAD binary -----------------------------------------------
find_openscad() {
  if [[ -n "${OPENSCAD:-}" ]]; then echo "$OPENSCAD"; return; fi
  local candidates=(
    "../build/OpenSCAD.app/Contents/MacOS/OpenSCAD"   # local macOS build
    "../build/openscad"                                # local Linux build
    "$(command -v openscad || true)"
    "$(command -v openscad-nightly || true)"
  )
  for c in "${candidates[@]}"; do
    [[ -n "$c" && -x "$c" ]] && { echo "$c"; return; }
  done
  echo ""; return
}

BIN="$(find_openscad)"
if [[ -z "$BIN" ]]; then
  echo "error: OpenSCAD binary not found. Set OPENSCAD=/path/to/openscad." >&2
  exit 1
fi

OUT=png
mkdir -p "$OUT"
SIZE=${SIZE:-1000,1000}

# Files that need a front view (vertical section through the ring axis).
front_view() {
  case "$1" in
    case_hole_mouth.scad|case_boss_fillet.scad) return 0 ;;
    *) return 1 ;;
  esac
}

# --- pick the files to render -------------------------------------------------
if [[ $# -gt 0 ]]; then
  files=("$@")
else
  files=(case_*.scad)
fi

echo "Using: $BIN"
for f in "${files[@]}"; do
  [[ -f "$f" ]] || { echo "skip (not found): $f" >&2; continue; }
  name="$(basename "$f" .scad)"
  if front_view "$f"; then rot="90,0,0"; else rot="0,0,0"; fi
  echo "rendering $f -> $OUT/$name.png  (rot $rot)"
  "$BIN" -o "$OUT/$name.png" \
    --imgsize="$SIZE" --projection=o \
    --camera="0,0,0,$rot,600" --viewall --autocenter \
    --render=1 "$f" >/dev/null 2>&1 \
    || { echo "  FAILED: $f" >&2; continue; }
done
echo "done -> $OUT/"
