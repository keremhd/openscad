#!/usr/bin/env bash
# Run one equivalence .scad and turn "empty output" into a passing test.
#
#   PASS (exit 0)  -> geometry is empty: candidate == reference within tolerance
#   FAIL (exit 1)  -> non-empty leftover: shapes differ (or operator not done yet)
#   ERROR (exit 2) -> binary missing / parse error / other failure
#
# Usage:
#   ./check_equivalence.sh eq_inner_corner.scad
#   OPENSCAD=/path/to/openscad ./check_equivalence.sh eq_selftest_equal.scad

set -uo pipefail
cd "$(dirname "$0")"

find_openscad() {
  if [[ -n "${OPENSCAD:-}" ]]; then echo "$OPENSCAD"; return; fi
  local candidates=(
    "../build/OpenSCAD.app/Contents/MacOS/OpenSCAD"
    "../build/openscad"
    "$(command -v openscad || true)"
    "$(command -v openscad-nightly || true)"
  )
  for c in "${candidates[@]}"; do
    [[ -n "$c" && -x "$c" ]] && { echo "$c"; return; }
  done
  echo ""
}

BIN="$(find_openscad)"
[[ -z "$BIN" ]] && { echo "ERROR: OpenSCAD binary not found (set OPENSCAD=...)" >&2; exit 2; }

f="${1:-}"
[[ -z "$f" ]] && { echo "usage: $0 <file.scad>" >&2; exit 2; }
[[ -f "$f" ]] || { echo "ERROR: no such file: $f" >&2; exit 2; }

tmp="$(mktemp -t fillet_eq_XXXXXX).stl"
trap 'rm -f "$tmp"' EXIT

out="$("$BIN" -o "$tmp" "$f" 2>&1)"

if grep -qi "top level object is empty" <<<"$out"; then
  echo "PASS  $f"
  exit 0
fi

# Non-empty, or a real error. Distinguish parse/eval errors from a genuine
# geometric difference so a broken test file isn't misread as "shapes differ".
if grep -qiE "error:|ERROR:|WARNING: Can't open|Parser error|syntax error" <<<"$out"; then
  echo "ERROR $f"
  echo "$out" | grep -iE "error|warning" | head -5 | sed 's/^/    /'
  exit 2
fi

echo "FAIL  $f  (non-empty leftover: shapes differ within tolerance)"
exit 1
