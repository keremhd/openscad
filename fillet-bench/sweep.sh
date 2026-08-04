#!/bin/zsh
# Sweep every bench model across a $fn axis and a radius axis. Numbers only --
# no PNG, no montage, no fonts. That is what makes it cheap enough to run over
# a matrix instead of over one point per model.
#
#   ./sweep.sh                              the whole cross, resuming
#   ./sweep.sh --selftest                   known answers only, ~1 min
#   ./sweep.sh --models tee,rib             substring filter, comma separated
#   ./sweep.sh --fn 14,26,32                override the $fn axis
#   ./sweep.sh --r 0.2,0.9                  override the radius axis
#   ./sweep.sh --models cross --grid        full fn x r product for one model
#   ./sweep.sh --fresh                      start the results file over
#
# WHY THIS EXISTS. expect.txt carries one $fn and one radius per model, so a
# fault that appears at some tessellations or some sizes and not others is
# invisible to the bench. Two open defects are exactly that shape:
# rib_into_boss is invalid at $fn 14 and 32 and valid at 26, and
# refused_neighbour is non-manifold at r 0.2/0.8/0.9/1.0 and valid at 0.5.
# --selftest asserts both, plus a facet-rotation invariant, before any sweep is
# worth reading.
#
# THE SHAPE OF THE SWEEP is a cross, not a grid: the $fn axis is walked at the
# model's own radius and the radius axis at stock defaults. The full product is
# 24 x 11 x 9 renders and most of it is redundant; --grid asks for it anyway on
# a filtered model.
#
# EVERY ROW IS APPENDED TO THE RESULTS FILE AS IT COMPLETES and re-running skips
# rows already there, because a 10-minute stall watchdog kills long silent
# commands and only finished work survives. Rows stream to stdout as well.
#
# WELD TOLERANCE IS ON EVERY ROW. The same mesh has read 205 non-manifold edges
# at 1e-5 and 0 at 1e-6 in this effort; a count without its tolerance is not a
# number. --tol changes it.
set -u
cd "${0:A:h}"

BIN=${BIN:-../build/OpenSCAD.app/Contents/MacOS/OpenSCAD}
FLAGS=${FLAGS:---enable=fillet}   # the modules are experimental; without this every model is an unknown-module error
TOL=${TOL:-1e-6}
CAP=${CAP:-240}                   # seconds per render; an unguarded Decompose() can take the machine down
OUT=${OUT:-results/sweep.tsv}
WORK=${WORK:-results/work}

FN_AXIS=(8 10 12 14 16 19 24 26 32 48 64)
R_AXIS=(0.2 0.3 0.5 0.8 0.9 1.0 1.5 2.0)
MODEL_FILTER=""
GRID=0
FRESH=0
SELFTEST=0

while (( $# )); do
  case $1 in
    --models)   MODEL_FILTER=$2; shift 2 ;;
    --fn)       FN_AXIS=(${(s:,:)2}); shift 2 ;;
    --r)        R_AXIS=(${(s:,:)2}); shift 2 ;;
    --tol)      TOL=$2; shift 2 ;;
    --out)      OUT=$2; shift 2 ;;
    --grid)     GRID=1; shift ;;
    --fresh)    FRESH=1; shift ;;
    --selftest) SELFTEST=1; shift ;;
    *) print -u2 "unknown argument: $1"; exit 2 ;;
  esac
done

[[ -x $BIN ]] || { print -u2 "no binary at $BIN -- set BIN="; exit 1; }

# Trap 1: make reports "Built target" for binaries that do not exist, and builds
# are silently discarded on this machine. Say what was measured, always, and say
# it loudly enough that a stale binary cannot be mistaken for a result.
print "binary: $BIN"
print "        built $(stat -f '%Sm' "$BIN")"
print "flags:  $FLAGS --backend=manifold"
print "weld:   $TOL"
stale=(${(f)"$(find ../src \( -name '*.cc' -o -name '*.h' \) -newer "$BIN" 2>/dev/null)"})
if (( ${#stale} )); then
  print "*** ${#stale} SOURCE FILES ARE NEWER THAN THE BINARY -- these numbers are about the old build ***"
  print -l ${stale[1,5]}
else
  print "sources newer than the binary: none"
fi
print ""

mkdir -p ${OUT:h} $WORK
(( FRESH )) && rm -f $OUT
if [[ ! -f $OUT ]]; then
  print "# model\tfn\tr\tvalid\tnonman\tchi\tgenus\tcomp\tv\te\tf\twarn\ttol\tsecs" > $OUT
fi

# --- one render + one reading ------------------------------------------------
# Sets the REPLY_* globals. fn == "def" means stock defaults with no -D FNSET,
# which is the only honest way to ask a planar model anything (trap 7).
run_one() {
  local model=$1 fn=$2 r=$3 extra=${4:-}
  local tag=${model}_fn${fn}_r${r}${extra:+_$extra}
  tag=${tag//[^A-Za-z0-9_.-]/_}
  local off=$WORK/$tag.off log=$WORK/$tag.log
  local args=()
  [[ $fn == def ]] || args+=(-D FNSET=$fn)
  [[ $r  == def ]] || args+=(-D R=$r)
  [[ -n $extra ]]  && args+=(-D $extra)

  rm -f $off
  local t0=$SECONDS
  # No timeout(1) on this machine (instrument 8), so cap it by hand: a render
  # that will not finish is a row, not a hung batch.
  ${=BIN} $=FLAGS --backend=manifold --render $args -o $off models/$model.scad > $log 2>&1 &
  local pid=$!
  local waited=0
  while kill -0 $pid 2>/dev/null; do
    sleep 1
    (( waited++ ))
    if (( waited >= CAP )); then kill -9 $pid 2>/dev/null; break; fi
  done
  wait $pid 2>/dev/null
  REPLY_SECS=$((SECONDS - t0))

  # Trap 5: a radius that does not fit is warned about, skipped, and still
  # exports a plausible mesh. An export succeeding proves nothing; read the log.
  REPLY_WARN=$(grep -c "WARNING:" $log 2>/dev/null); REPLY_WARN=${REPLY_WARN:-0}

  if [[ ! -s $off ]]; then
    REPLY_VALID=NOOUT; REPLY_NONMAN=-; REPLY_CHI=-; REPLY_GENUS=-
    REPLY_COMP=-; REPLY_V=-; REPLY_E=-; REPLY_F=-
    (( waited >= CAP )) && REPLY_VALID=TIMEOUT
    return
  fi
  local line=$(python3 mesh.py --tol $TOL $off 2>&1)
  REPLY_VALID=$(print -r -- "$line" | sed -n 's/.*off: \([A-Z-]*\).*/\1/p')
  get() { print -r -- "$line" | sed -n "s/.* $1=\([^ ]*\).*/\1/p"; }
  REPLY_NONMAN=$(get nonman); REPLY_CHI=$(get chi); REPLY_GENUS=$(get genus)
  REPLY_COMP=$(get comp); REPLY_V=$(get v); REPLY_E=$(get e); REPLY_F=$(get f)
  [[ -n $REPLY_VALID ]] || REPLY_VALID=UNREADABLE
}

emit() {  # model fn r  -- render, print, checkpoint. Skips a row already done.
  local model=$1 fn=$2 r=$3
  if grep -q "^$model	$fn	$r	" $OUT 2>/dev/null; then
    print "  = $model fn=$fn r=$r (already in $OUT)"
    return
  fi
  run_one $model $fn $r
  local row="$model	$fn	$r	$REPLY_VALID	$REPLY_NONMAN	$REPLY_CHI	$REPLY_GENUS	$REPLY_COMP	$REPLY_V	$REPLY_E	$REPLY_F	$REPLY_WARN	$TOL	$REPLY_SECS"
  print -r -- "$row" >> $OUT
  printf "  %-18s fn=%-4s r=%-5s %-9s nonman=%-3s chi=%-3s genus=%-4s warn=%-3s tol=%s %ss\n" \
    $model $fn $r $REPLY_VALID $REPLY_NONMAN $REPLY_CHI $REPLY_GENUS $REPLY_WARN $TOL $REPLY_SECS
}

# --- the instrument's own acceptance test ------------------------------------
# Nine instruments in this effort have been found broken. This one states its
# known answers up front and refuses to be trusted without them.
if (( SELFTEST )); then
  fails=0
  check() {  # label expected-VALID/INVALID model fn r [extra]
    local label=$1 want=$2; shift 2
    run_one "$@"
    local got=$REPLY_VALID
    if [[ $got == $want* ]]; then
      print "  PASS  $label -> $got nonman=$REPLY_NONMAN chi=$REPLY_CHI tol=$TOL"
    else
      print "  FAIL  $label -> $got (wanted $want) nonman=$REPLY_NONMAN chi=$REPLY_CHI tol=$TOL"
      (( fails++ ))
    fi
  }

  print "known answers -- rib_into_boss across \$fn"
  check "rib_into_boss fn=14" INVALID rib_into_boss 14 def
  check "rib_into_boss fn=26" VALID   rib_into_boss 26 def
  check "rib_into_boss fn=32" INVALID rib_into_boss 32 def
  print "known answers -- refused_neighbour across r"
  check "refused_neighbour r=0.2" INVALID refused_neighbour def 0.2
  check "refused_neighbour r=0.5" VALID   refused_neighbour def 0.5
  check "refused_neighbour r=0.8" INVALID refused_neighbour def 0.8
  check "refused_neighbour r=0.9" INVALID refused_neighbour def 0.9
  check "refused_neighbour r=1.0" INVALID refused_neighbour def 1.0

  # The axes must actually arrive. If -D were being dropped, every check above
  # would still pass on whichever answer the default happens to give, and the
  # sweep would be a column of one number reported eleven times.
  print "plumbing -- the axes must move the mesh"
  run_one cyl_control 12 def; a=$REPLY_V
  run_one cyl_control 48 def; b=$REPLY_V
  if [[ $a != $b ]]; then print "  PASS  -D FNSET reaches the model (cyl_control v: $a at 12, $b at 48)"
  else print "  FAIL  -D FNSET changes nothing (v=$a both) -- the \$fn axis is not plumbed"; (( fails++ )); fi
  run_one refused_neighbour def 0.5; a=$REPLY_V
  run_one refused_neighbour def 0.9; b=$REPLY_V
  if [[ $a != $b ]]; then print "  PASS  -D R reaches the model (refused_neighbour v: $a at 0.5, $b at 0.9)"
  else print "  FAIL  -D R changes nothing (v=$a both) -- the radius axis is not plumbed"; (( fails++ )); fi

  # The self-proving invariant: a solid rotated by exactly one facet is the same
  # solid, and must measure the same. This is the one check that can fail on a
  # reader that is right about every known answer and still wrong.
  print "self-proof -- a facet rotation is a rigid motion"
  for f in 16 24 32; do
    run_one selfproof $f def "ROT=0"; s0="$REPLY_VALID v=$REPLY_V e=$REPLY_E f=$REPLY_F chi=$REPLY_CHI nonman=$REPLY_NONMAN"
    run_one selfproof $f def "ROT=1"; s1="$REPLY_VALID v=$REPLY_V e=$REPLY_E f=$REPLY_F chi=$REPLY_CHI nonman=$REPLY_NONMAN"
    if [[ $s0 == $s1 ]]; then print "  PASS  selfproof fn=$f rot 0 == rot 1: $s0"
    else print "  FAIL  selfproof fn=$f rot 0 != rot 1\n        rot0: $s0\n        rot1: $s1"; (( fails++ )); fi
  done

  print ""
  if (( fails )); then print "SELFTEST FAILED: $fails checks. The sweep is not to be believed."; exit 1; fi
  print "SELFTEST PASSED. Weld tolerance $TOL."
  exit 0
fi

# --- the sweep ---------------------------------------------------------------
# expect.txt is the model list and the authority on which models have a $fn axis
# at all: "-" and "skip" mean no curvature, and sweeping $fn over a planar model
# tests nothing (trap 7). One whole reported series has been vacuous that way.
models=(); tess=()
while read -r name fn rest; do
  [[ -z $name || $name == \#* ]] && continue
  if [[ -n $MODEL_FILTER ]]; then
    hit=0
    for pat in ${(s:,:)MODEL_FILTER}; do [[ $name == *$pat* ]] && hit=1; done
    (( hit )) || continue
  fi
  models+=($name)
  [[ $fn == - || $fn == skip ]] || tess+=($name)
done < expect.txt

(( ${#models} )) || { print -u2 "no models matched '$MODEL_FILTER'"; exit 1; }
print "${#models} models (${#tess} with a \$fn axis), results -> $OUT\n"

for m in $models; do
  has_fn=0; for t in $tess; do [[ $t == $m ]] && has_fn=1; done
  # The model's own radius, so the $fn axis is walked at the size the bench
  # already reports and the two axes cross at a row with a known answer.
  own=$(sed -n 's/^R = \([0-9.]*\);.*/\1/p' models/$m.scad | head -1)
  own=${own:-def}

  if (( GRID )); then
    fns=(def); (( has_fn )) && fns=(def $FN_AXIS)
    for fn in $fns; do for r in def $R_AXIS; do emit $m $fn $r; done; done
    continue
  fi

  emit $m def def                                   # the row the bench already carries
  if (( has_fn )); then
    for fn in $FN_AXIS; do emit $m $fn def; done    # $fn axis at the model's own radius
  else
    print "  - $m has no curvature; \$fn axis skipped (trap 7)"
  fi
  for r in $R_AXIS; do
    [[ $r == $own ]] && continue                    # already covered by the defaults row
    emit $m def $r                                  # radius axis at stock defaults
  done
done

print "\ndone. $(( $(wc -l < $OUT) - 1 )) rows in $OUT, weld tolerance $TOL"
print "invalid rows:"
awk -F'\t' 'NR>1 && $4 != "VALID" {print "  " $1 "  fn=" $2 "  r=" $3 "  " $4 "  nonman=" $5 "  chi=" $6 "  warn=" $12 "  tol=" $13}' $OUT
