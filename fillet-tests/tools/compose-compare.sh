#!/bin/sh
# Compare the ways the inner and outer blend passes can be composed, on one model.
#
# The passes can be arranged in more than one order, and the arrangements differ
# only where a bead meets a rounded edge -- which is exactly the place that is
# hard to see and easy to get wrong. This runs each arrangement on the same model
# and reports genus, warning count, connected components and volume together,
# because no one of those catches every failure: a result can be genus 0 and still
# have shed crumbs, and it can be crumb-free and still have silently dropped every
# bead.
#
#   ./compose-compare.sh model.scad [r_inner] [r_outer] [fn]
#
# `model.scad` must define `module m()`. Set OPENSCAD to override the binary.
#
# The arrangements:
#   today     both tools built from the original child
#   chained   the outer pass runs on the solid the inner pass produced
#   brushed   the inner pass is brushed to keep out of the outer pass's region,
#             using that pass's own tool solid as a negative brush
#   inner     the inner pass alone
#   outer     the outer pass alone

set -e

MODEL=${1:?usage: compose-compare.sh model.scad [r_inner] [r_outer] [fn]}
RI=${2:-2}
RO=${3:-2}
FN=${4:-32}
OPENSCAD=${OPENSCAD:-build/OpenSCAD.app/Contents/MacOS/OpenSCAD}
TOOLS=$(cd "$(dirname "$0")" && pwd)

[ -x "$OPENSCAD" ] || { echo "no OpenSCAD at $OPENSCAD (set OPENSCAD=)"; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
MODEL_ABS=$(cd "$(dirname "$MODEL")" && pwd)/$(basename "$MODEL")

emit() { # $1 = name, $2 = body
  cat > "$WORK/$1.scad" <<EOF
\$fn = $FN;
RI = $RI; RO = $RO;
include <$MODEL_ABS>
$2
EOF
}

emit today '
difference() {
    union() { m(); fillet_tool(r = RI) m(); }
    round_tool(r = RO) m();
}'

emit chained '
module U() { union() { m(); fillet_tool(r = RI) m(); } }
difference() { U(); round_tool(r = RO) U(); }'

emit brushed '
difference() {
    union() {
        m();
        fillet_tool(r = RI) {
            m();
            difference() { hull() m(); round_tool(r = RO) m(); }
        }
    }
    round_tool(r = RO) m();
}'

emit inner '
union() { m(); fillet_tool(r = RI) m(); }'

emit outer '
difference() { m(); round_tool(r = RO) m(); }'

printf '%s  r_inner=%s r_outer=%s $fn=%s\n\n' "$(basename "$MODEL")" "$RI" "$RO" "$FN"
printf '%-9s %-7s %-6s %s\n' arrangement genus warn 'components / volume'

for name in today chained brushed inner outer; do
  out=$("$OPENSCAD" --backend=manifold -o "$WORK/$name.stl" "$WORK/$name.scad" 2>&1 || true)
  genus=$(printf '%s' "$out" | sed -n 's/.*Genus: *\(-*[0-9]*\).*/\1/p' | head -1)
  warn=$(printf '%s' "$out" | grep -c WARNING || true)
  if [ -f "$WORK/$name.stl" ]; then
    stat=$(python3 "$TOOLS/meshstat.py" "$WORK/$name.stl" | sed 's/^[^ ]*  *//')
  else
    stat='(no output)'
  fi
  printf '%-9s %-7s %-6s %s\n' "$name" "${genus:-?}" "$warn" "$stat"
done
