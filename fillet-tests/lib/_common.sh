#!/usr/bin/env bash
# Shared plumbing for render.sh, check.sh and run_all.sh: locating the binary,
# and reading a case's metadata back out of the case file itself.
#
# Case metadata (sign, slice, variants) lives in the .scad and nowhere else. The
# shell gets at it by evaluating the file with OpenSCAD's echo export, which runs
# the script without rendering any geometry and costs about a quarter of a
# second. That way a variant can never be listed in one place and forgotten in
# the other.

find_openscad() {
  if [[ -n "${OPENSCAD:-}" ]]; then echo "$OPENSCAD"; return; fi
  local c
  for c in "$ROOT/../build/OpenSCAD.app/Contents/MacOS/OpenSCAD" \
           "$ROOT/../build/openscad" \
           "$(command -v openscad || true)" \
           "$(command -v openscad-nightly || true)"; do
    [[ -n "$c" && -x "$c" ]] && { echo "$c"; return; }
  done
  echo ""
}

require_openscad() {
  BIN="$(find_openscad)"
  [[ -z "$BIN" ]] && { echo "ERROR: OpenSCAD binary not found (set OPENSCAD=...)" >&2; exit 2; }
}

# The fillet nodes are Manifold-only; ask for it explicitly rather than relying
# on whatever the build defaults to.
BACKEND_ARGS=(--backend=Manifold)

# Dilation goes through CGAL's Nef kernel, which on some inputs takes minutes or
# does not finish at all. One such case must not wedge the whole suite, so every
# render and check is bounded. Returns 124 on timeout, mirroring GNU timeout(1),
# which macOS does not ship.
CHECK_TIMEOUT=${CHECK_TIMEOUT:-60}

run_bounded() {  # logfile cmd...
  local log="$1"; shift
  "$@" > "$log" 2>&1 &
  local pid=$! waited=0
  while kill -0 "$pid" 2>/dev/null; do
    if [[ $waited -ge $CHECK_TIMEOUT ]]; then
      kill -9 "$pid" 2>/dev/null
      wait "$pid" 2>/dev/null
      return 124
    fi
    sleep 1
    waited=$((waited + 1))
  done
  wait "$pid"
}

case_path() {  # accepts a bare name, a name.scad, or a path
  local f="$1"
  [[ -f "$f" ]] && { cd "$(dirname "$f")" >/dev/null && echo "$PWD/$(basename "$f")"; return; }
  [[ -f "$ROOT/cases/$f" ]] && { echo "$ROOT/cases/$f"; return; }
  [[ -f "$ROOT/cases/$f.scad" ]] && { echo "$ROOT/cases/$f.scad"; return; }
  echo ""
}

# Echo one line per variant plus the case sign and slice kind:
#   SIGN <union|subtract>
#   SLICE <top|front|stack>
#   VARIANT <index> <name> <size> <has_ref> <tol>
probe_case() {
  local case_abs="$1" tmp
  tmp="$(mktemp -d)"
  cat > "$tmp/probe.scad" <<EOF
include <$case_abs>;
FILLET_DRIVER = true;
echo(str("SIGN ", CASE_SIGN));
echo(str("SLICE ", CASE_SLICE[0]));
for (i = [0 : len(CASE_VARIANTS) - 1])
  echo(str("VARIANT ", i, " ", CASE_VARIANTS[i][0], " ", CASE_VARIANTS[i][1], " ",
           CASE_VARIANTS[i][2], " ", CASE_VARIANTS[i][3]));
EOF
  "$BIN" -o "$tmp/probe.echo" "$tmp/probe.scad" >/dev/null 2>&1
  sed -nE 's/^ECHO: "((SIGN|SLICE|VARIANT) [^"]*)"$/\1/p' "$tmp/probe.echo"
  rm -rf "$tmp"
}

# Which checks apply to a variant. The reference comparison is skipped where the
# case declares it has no hand-written answer (the junction corners, and the
# oversized radii whose clamping behaviour is not settled) — those variants still
# run the reference-free checks rather than dropping out of the suite.
checks_for_variant() {  # has_ref
  if [[ "$1" == "true" ]]; then echo "tool sandwich emits"; else echo "sandwich emits"; fi
}
