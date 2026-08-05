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
#   ./sweep.sh --repeat 5                   renders per cell (default 3)
#   ./sweep.sh --fresh                      start the results file over
#
# WHY THIS EXISTS. expect.txt carries one $fn and one radius per model, so a
# fault that appears at some tessellations or some sizes and not others is
# invisible to the bench. Two open defects are exactly that shape:
# rib_into_boss is invalid at several $fn and valid at others, and
# refused_neighbour is non-manifold at r 0.2/0.8/0.9/0.95/1.0/1.05 and valid at
# 0.3, 0.5 and 1.2. --selftest asserts both, a facet-rotation invariant, and
# that both models are deterministic, before any sweep here is worth reading.
#
# IT READS EXACT ASCII STL, not OFF. export_off.cc streams at the default six
# significant figures and has been caught merging two vertices 5.7e-7 mm apart.
# The OFF is exported and read anyway so the loss is visible in the `agree`
# column rather than silent. Measured, it changes no verdict -- but that is a
# finding, not an assumption, and it is re-measured on every row.
#
# PIN THE BINARY. It was replaced under this instrument four times in one
# session: cp -Rp the build aside and point BIN at the copy. Every row records
# the md5 of the binary that produced it.
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
REPEAT=${REPEAT:-3}                # renders per cell; the builder is nondeterministic, see emit()
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
    --repeat)   REPEAT=$2; shift 2 ;;
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
print "reader: exact ASCII STL, OFF read alongside and compared"
print "weld:   $TOL"
print "runs:   $REPEAT per cell"
stale=(${(f)"$(find ../src \( -name '*.cc' -o -name '*.h' \) -newer "$BIN" 2>/dev/null)"})
if (( ${#stale} )); then
  print "*** ${#stale} SOURCE FILES ARE NEWER THAN THE BINARY -- these numbers are about the old build ***"
  print -l ${stale[1,5]}
else
  print "sources newer than the binary: none"
fi
print ""

# Trap 1 again, per row rather than per run: the binary was replaced under this
# instrument FOUR times in one session by other worktrees. Every row carries the
# md5 of the binary that produced it -- md5 rather than mtime, because a rebuild
# of identical bytes is the same instrument and a copy is not a different one.
# A results file that mixes two builds now says so instead of reading as one
# coherent table. Pin a build by copying it aside and setting BIN to the copy.
BINSTAMP=$(md5 -q "$BIN" | cut -c1-8)

mkdir -p ${OUT:h} $WORK
(( FRESH )) && rm -f $OUT
if [[ ! -f $OUT ]]; then
  # nmvert, throat and wantcomp are APPENDED at the end rather than put beside
  # nonman and genus where they belong, so that every field index in the awk
  # summaries below and in every recorded results file stays what it was. A
  # column inserted in the middle silently reassigns eighteen of them.
  print "# model\tfn\tr\tvalid\tnonman\tchi\tgenus\tcomp\tv\te\tf\twarn\ttol\truns\tdistinct\tagree\tsecs\tbin\tnmvert\tthroat\twantcomp" > $OUT
fi

# --- one render + one reading ------------------------------------------------
# Sets the REPLY_* globals. fn == "def" means stock defaults with no -D FNSET,
# which is the only honest way to ask a planar model anything (trap 7).

# How many solids the MODEL'S SOURCE builds. Declared in the .scad itself with a
# `// mesh.py-comp: N` line, because it is a fact about the source and not about
# the sweep; everything that declares nothing builds one solid, and a one-solid
# model that comes back in two pieces has shed a fragment. shallow_crease is the
# only declarer -- it renders two plates as its whole point, and its nine comp=2
# rows are correct.
want_comp() {
  local n=$(sed -n 's|^// *mesh\.py-comp: *\([0-9][0-9]*\).*|\1|p' models/$1.scad | head -1)
  print -r -- ${n:-1}
}

run_one() {
  local model=$1 fn=$2 r=$3 extra=${4:-}
  local tag=${model}_fn${fn}_r${r}${extra:+_$extra}
  tag=${tag//[^A-Za-z0-9_.-]/_}
  local off=$WORK/$tag.off stl=$WORK/$tag.stl log=$WORK/$tag.log
  local args=()
  [[ $fn == def ]] || args+=(-D FNSET=$fn)
  [[ $r  == def ]] || args+=(-D R=$r)
  [[ -n $extra ]]  && args+=(-D $extra)

  rm -f $off $stl
  local t0=$SECONDS
  # ONE render, BOTH formats. OpenSCAD accepts repeated -o, so the OFF and the
  # STL are the same solid rather than two renders of a model that may not
  # render the same twice -- which matters here, because rib_into_boss does not.
  # No timeout(1) on this machine (instrument 8), so cap it by hand: a render
  # that will not finish is a row, not a hung batch.
  ${=BIN} $=FLAGS --backend=manifold --render $args -o $off -o $stl models/$model.scad > $log 2>&1 &
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

  local wc=$(want_comp $model)
  if [[ ! -s $stl ]]; then
    REPLY_VALID=NOOUT; REPLY_NONMAN=-; REPLY_CHI=-; REPLY_GENUS=-
    REPLY_COMP=-; REPLY_V=-; REPLY_E=-; REPLY_F=-; REPLY_AGREE=-
    REPLY_NMVERT=-; REPLY_THROAT=-
    (( waited >= CAP )) && REPLY_VALID=TIMEOUT
    return
  fi
  # THE STL IS THE READING. export_stl.cc prints through ToShortest, an exact
  # double round trip; export_off.cc streams at the default six significant
  # figures and has been shown to merge two vertices 5.7e-7 mm apart at a
  # coordinate of 1.2. The OFF is still read, and any disagreement is recorded
  # rather than left silent -- see the `agree` column and README.
  if [[ -s $off ]]; then
    python3 mesh.py --compare --tol $TOL --comp $wc $off $stl > $WORK/$tag.cmp 2>&1
    if grep -q '^AGREE' $WORK/$tag.cmp; then REPLY_AGREE=yes; else REPLY_AGREE=NO; fi
  else
    REPLY_AGREE=-
  fi
  local line=$(python3 mesh.py --tol $TOL --comp $wc $stl 2>&1)
  # awk, not sed: BSD sed has no \| alternation, and a pattern anchored on
  # "off:" silently stopped matching the moment the reader moved to STL --
  # which showed up as UNREADABLE on every row rather than as an error.
  REPLY_VALID=$(print -r -- "$line" | awk '{print $2}')
  # awk on whole tokens and take the FIRST match, not sed: `.*comp=` is greedy,
  # and the line can end "(wanted comp=1)", so a sed read of comp returned the
  # EXPECTATION instead of the measurement -- a column that would have reported
  # every shed fragment as comp=1 and agreed with itself.
  get() { print -r -- "$line" | awk -v k="$1" \
      '{for(i=1;i<=NF;i++) if (index($i, k "=")==1) {print substr($i, length(k)+2); exit}}'; }
  REPLY_NONMAN=$(get nonman); REPLY_CHI=$(get chi); REPLY_GENUS=$(get genus)
  REPLY_COMP=$(get comp); REPLY_V=$(get v); REPLY_E=$(get e); REPLY_F=$(get f)
  REPLY_NMVERT=$(get nmvert)
  REPLY_THROAT=$(print -r -- "$line" | sed -n 's/.*throat<=\([^ ]*\)mm.*/\1/p')
  REPLY_THROAT=${REPLY_THROAT:--}
  REPLY_WANTCOMP=$wc
  [[ -n $REPLY_VALID ]] || REPLY_VALID=UNREADABLE
}

# A cell is rendered REPEAT times, not once. That was a measured requirement
# before 2026-08-05: rib_into_boss at $fn=14 returned a VALID mesh on 2 of 55
# identical invocations of the same binary and came back INVALID on the other 53,
# and the two good readings were two DIFFERENT meshes (v=228 f=452 and
# v=226 f=448) against the usual v=226 f=450 -- the topology moved, not just the
# vertex order, so one run per cell reported a fault as clean about once every
# thirty cells. The cause was an out-of-bounds read in chainBulges() and it is
# fixed; $fn=14 is now 60/60 one mesh. The repeat is KEPT anyway, because it is
# what would detect the read coming back, and because `distinct` is then a
# measurement rather than an assumption.
#
# Runs are aggregated to the WORST outcome, because a solid that is invalid on
# any run is not a valid solid, and the disagreement is recorded rather than
# discarded: `runs` is how many were rendered, `distinct` how many different
# meshes came back. distinct > 1 is itself a finding.
emit() {  # model fn r  -- render REPEAT times, print, checkpoint. Skips a done row.
  local model=$1 fn=$2 r=$3
  if grep -q "^$model	$fn	$r	" $OUT 2>/dev/null; then
    print "  = $model fn=$fn r=$r (already in $OUT)"
    return
  fi
  local -a seen
  local worst="" wnonman wchi wgenus wcomp wv we wf wwarn wagree=yes wnmvert wthroat wwant
  local t0=$SECONDS i
  for i in $(seq 1 $REPEAT); do
    run_one $model $fn $r
    local sig="$REPLY_VALID v=$REPLY_V e=$REPLY_E f=$REPLY_F chi=$REPLY_CHI nonman=$REPLY_NONMAN nmvert=$REPLY_NMVERT comp=$REPLY_COMP"
    [[ ${seen[(Ie)$sig]} -eq 0 ]] && seen+=($sig)
    # Keep the first run, then let any non-VALID run displace it.
    if [[ -z $worst || ( $worst == VALID && $REPLY_VALID != VALID ) \
          || ( $worst == INVALID && ( $REPLY_VALID == NOOUT || $REPLY_VALID == TIMEOUT ) ) ]]; then
      worst=$REPLY_VALID; wnonman=$REPLY_NONMAN; wchi=$REPLY_CHI; wgenus=$REPLY_GENUS
      wcomp=$REPLY_COMP; wv=$REPLY_V; we=$REPLY_E; wf=$REPLY_F; wwarn=$REPLY_WARN
      wnmvert=$REPLY_NMVERT; wthroat=$REPLY_THROAT; wwant=${REPLY_WANTCOMP:-1}
    fi
    [[ $REPLY_AGREE == NO ]] && wagree=NO
  done
  local distinct=${#seen}
  local secs=$((SECONDS - t0))
  local row="$model	$fn	$r	$worst	$wnonman	$wchi	$wgenus	$wcomp	$wv	$we	$wf	$wwarn	$TOL	$REPEAT	$distinct	$wagree	$secs	$BINSTAMP	$wnmvert	$wthroat	$wwant"
  print -r -- "$row" >> $OUT
  printf "  %-18s fn=%-4s r=%-5s %-9s nonman=%-3s nmvert=%-3s chi=%-3s genus=%-4s comp=%s/%s warn=%-3s tol=%s runs=%s distinct=%s off=%s %ss%s\n" \
    $model $fn $r $worst $wnonman $wnmvert $wchi $wgenus $wcomp $wwant $wwarn $TOL $REPEAT $distinct $wagree $secs \
    "$( (( distinct > 1 )) && print '  <-- RUNS DISAGREE' )$( [[ $wagree == NO ]] && print '  <-- OFF/STL DISAGREE' )"
}

# --- the instrument's own acceptance test ------------------------------------
# Nine instruments in this effort have been found broken. This one states its
# known answers up front and refuses to be trusted without them.
if (( SELFTEST )); then
  fails=0
  # Aggregated over REPEAT runs exactly as emit() does, and for the same reason:
  # a single run of rib_into_boss at $fn=14 comes back clean about once in
  # thirty, and a selftest that fails one time in thirty is not a selftest. This
  # asymmetry is deliberate -- an INVALID expectation is met if ANY run is
  # invalid, a VALID expectation only if EVERY run is.
  check() {  # label expected-VALID/INVALID model fn r [extra]
    local label=$1 want=$2; shift 2
    local worst="" nm chi n nv cp wc
    for n in $(seq 1 $REPEAT); do
      run_one "$@"
      if [[ -z $worst || ( $worst == VALID && $REPLY_VALID != VALID ) ]]; then
        worst=$REPLY_VALID; nm=$REPLY_NONMAN; chi=$REPLY_CHI
        nv=$REPLY_NMVERT; cp=$REPLY_COMP; wc=$REPLY_WANTCOMP
      fi
    done
    local shown="nonman=$nm nmvert=$nv chi=$chi comp=$cp/$wc tol=$TOL ($REPEAT runs)"
    if [[ $worst == $want* ]]; then
      print "  PASS  $label -> $worst $shown"
    else
      print "  FAIL  $label -> $worst (wanted $want) $shown"
      (( fails++ ))
    fi
  }

  # The instrument's own instrument, first: five synthetic solids whose answers
  # are arithmetic rather than measurement. If the pinch check cannot tell two
  # cubes meeting at a corner from a cube, nothing below is worth rendering.
  print "the reader's synthetic controls"
  if python3 mesh.py --selftest --tol $TOL | sed 's/^/  /'; then
    :
  else
    print "  FAIL  mesh.py's own selftest failed -- the reader is broken, stop here"
    (( fails++ ))
  fi

  # KNOWN ANSWERS, re-derived 2026-08-05 on a pinned binary and read from exact
  # ASCII STL. They are NOT the numbers this file shipped with, and the change
  # was not a correction of the instrument -- the code moved under it and the old
  # expectations were kept past their evidence. Do not restore a number here
  # because it is written down somewhere.
  #
  # Every one of these is a DISTRIBUTION, not a value: rib_into_boss returns two
  # or three different meshes across eight identical runs at every $fn tested.
  # Only $fn=14 flips validity (6 valid / 2 invalid in 8), so it is not asserted
  # as either answer -- it is asserted as flaky, below.
  print "known answers -- rib_into_boss across \$fn (nondeterministic model)"
  check "rib_into_boss fn=11" INVALID rib_into_boss 11 def
  check "rib_into_boss fn=25" INVALID rib_into_boss 25 def
  check "rib_into_boss fn=32" INVALID rib_into_boss 32 def
  check "rib_into_boss fn=26" VALID   rib_into_boss 26 def
  check "rib_into_boss fn=48" VALID   rib_into_boss 48 def
  print "known answers -- refused_neighbour across r (deterministic model)"
  check "refused_neighbour r=0.2"  INVALID refused_neighbour def 0.2
  check "refused_neighbour r=0.8"  INVALID refused_neighbour def 0.8
  check "refused_neighbour r=0.9"  INVALID refused_neighbour def 0.9
  check "refused_neighbour r=0.95" INVALID refused_neighbour def 0.95
  check "refused_neighbour r=1.0"  INVALID refused_neighbour def 1.0
  check "refused_neighbour r=1.05" INVALID refused_neighbour def 1.05
  check "refused_neighbour r=0.3"  VALID   refused_neighbour def 0.3
  check "refused_neighbour r=0.5"  VALID   refused_neighbour def 0.5
  check "refused_neighbour r=1.2"  VALID   refused_neighbour def 1.2

  # KNOWN ANSWERS FOR THE CORRECTED CRITERION, dissected 2026-08-05 and each
  # asserted on the FAULT and not only on the verdict. Every one of these read
  # VALID before the pinch check and the component expectation existed, so an
  # assertion here that starts passing for a different reason than the one named
  # is the failure mode to watch -- which is why `want` is a field value, not a
  # word. `pinches` and `parts` in --json carry the locations and the sizes.
  print "known answers -- the debris the old criterion could not see"
  field() {  # label model fn r  key=value...
    local label=$1 model=$2 fn=$3 r=$4; shift 4
    run_one $model $fn $r
    local wc=$(want_comp $model)
    local line=$(python3 mesh.py --tol $TOL --comp $wc \
        $WORK/${model}_fn${fn}_r${r}.stl 2>&1)
    local bad=()
    local kv k want got
    for kv in "$@"; do
      k=${kv%%=*}; want=${kv#*=}
      if [[ $k == valid ]]; then
        got=$(print -r -- "$line" | awk '{print $2}')
      else
        got=$(print -r -- "$line" | awk -v k="$k" \
          '{for(i=1;i<=NF;i++) if (index($i, k "=")==1) {print substr($i, length(k)+2); exit}}')
      fi
      [[ $got == $want ]] || bad+=("$k=$got wanted $want")
    done
    if (( ${#bad} )); then
      print "  FAIL  $label -- ${bad[*]}"
      print "        $line"
      (( fails++ ))
    else
      print "  PASS  $label -- $*"
    fi
  }
  # Two point-attached slivers, 4 and 8 triangles, and NO tunnel anywhere. This
  # cell read VALID chi=4 genus=1 comp=3: two pinches make an even chi, and the
  # genus was a number with no referent.
  field "tee r=0.5 is two pinches, not a handle" tee def 0.5 \
        valid=INVALID nmvert=2 nonman=0 comp=3 genus=n/a chi=4
  # A fully detached 6-triangle fragment, 0.35 x 0.10 x 0.40 mm, sharing zero
  # vertices with the body. Nothing about it is non-manifold; it is a second
  # component of a model that unions one solid.
  field "tee_small sheds a detached fragment (defaults)" tee_small def def \
        valid=INVALID comp=2 nmvert=0 nonman=0
  field "tee_small sheds it at \$fn=10 too" tee_small 10 def \
        valid=INVALID comp=2
  field "tee_small sheds it at r=1.5 too" tee_small def 1.5 \
        valid=INVALID comp=2
  # A 4-triangle shard of 3.45e-7 mm^3 floating OUTSIDE the solid.
  field "cross r=0.9 sheds a shard outside the body" cross def 0.9 \
        valid=INVALID comp=2 nmvert=0 nonman=0
  # The chi-odd family: one tetrahedral sliver attached at exactly one vertex.
  # It was already invalid, by parity; it must now be invalid for the reason.
  field "tee r=0.9 is one pinch, located" tee def 0.9 \
        valid=INVALID nmvert=1 chi=3 genus=n/a
  field "tee_oblique r=0.2 is one pinch" tee_oblique def 0.2 valid=INVALID nmvert=1
  field "tee_oblique r=0.3 is one pinch" tee_oblique def 0.3 valid=INVALID nmvert=1
  field "tee_oblique r=0.8 is one pinch" tee_oblique def 0.8 valid=INVALID nmvert=1
  # comp > 1 is a fault only against what the model builds. shallow_crease
  # renders two plates as its whole point and declares so in its source; its
  # nine comp=2 rows are correct and must stay green.
  field "shallow_crease legitimately renders two plates" shallow_crease def def \
        valid=VALID comp=2 nmvert=0
  # And the one real handle in the bench stays valid, now carrying the throat
  # that says why it does not matter.
  field "cross r=2.0 genus 4 is real, and micron-scale" cross def 2.0 \
        valid=VALID genus=4 comp=1 nmvert=0
  # THE PAIR IS THE CHECK, not either half. cross's four handles must close
  # under a weld the tessellation still survives, and hole_plate's genus 1 --
  # a hole a plate is supposed to have, at the same genus and the same every
  # other number -- must not. An instrument that cannot separate those two is
  # not triage, and the first two versions of the throat proxy could not.
  run_one cross def 2.0
  local cross_t=$REPLY_THROAT
  run_one hole_plate def def
  local hole_t=$REPLY_THROAT
  if [[ $cross_t == - || -z $cross_t ]]; then
    print "  FAIL  cross r=2.0 reported no throat beside a nonzero genus"
    (( fails++ ))
  elif (( cross_t <= 0.01 )); then
    print "  PASS  cross r=2.0 handles close under a ${cross_t}mm weld -- micron-scale, 20x under a 0.2 mm layer"
  else
    print "  FAIL  cross r=2.0 throat<=${cross_t}mm -- that is not the micron-scale handle the record describes"
    (( fails++ ))
  fi
  if [[ $hole_t == - || -z $hole_t ]]; then
    print "  PASS  hole_plate's genus 1 survives every weld the tessellation allows, as a real hole must"
  else
    print "  FAIL  hole_plate's hole closed under a ${hole_t}mm weld -- the throat proxy is reading facet spacing again"
    (( fails++ ))
  fi

  # THE BUILDER IS NOW DETERMINISTIC, and this pair is what proves it.
  # rib_into_boss used to return two or three distinct meshes over eight runs at
  # every $fn tested; the cause was an out-of-bounds read in chainBulges() --
  # a section overrunning a seam vertex took a negative chain parameter and
  # `static_cast<int>(floor(q)) % nsta` stayed negative, so the builder read the
  # 24 bytes before a station buffer and used whatever the allocator had left
  # there. Fixed 2026-08-05; measured after the fix at 60/60 identical meshes at
  # $fn=14 and 40/40 at R=1.0, all rc=0, no SIGBUS.
  #
  # So the assertion is INVERTED from what this file used to carry: both models
  # must now be deterministic, and a second mesh appearing here means the read
  # is back (or a new one is). This check is the reason the sweep's `distinct`
  # column can be believed. 16 runs is enough that the old 6/2 split would be
  # caught essentially every time.
  print "the determinism pair -- neither model may return two meshes"
  spread() {  # maxruns model fn r -- distinct meshes, early exit once >1
    local maxn=$1; shift
    local -a sigs; local n sig
    for n in $(seq 1 $maxn); do
      run_one "$@"
      sig="v=$REPLY_V e=$REPLY_E f=$REPLY_F"
      [[ ${sigs[(Ie)$sig]} -eq 0 ]] && sigs+=($sig)
      (( ${#sigs} > 1 )) && break
    done
    REPLY_SPREAD=${#sigs}; REPLY_RUNS=$n
  }
  spread 16 rib_into_boss 32 def
  if (( REPLY_SPREAD == 1 )); then
    print "  PASS  rib_into_boss fn=32 gave one mesh across $REPLY_RUNS runs, as it must since the seam-vertex read was fixed"
  else
    print "  FAIL  rib_into_boss fn=32 gave $REPLY_SPREAD distinct meshes in $REPLY_RUNS runs -- the builder is reading uninitialised memory again; every number below is void"
    (( fails++ ))
  fi
  spread $REPEAT refused_neighbour def 0.9
  if (( REPLY_SPREAD == 1 )); then
    print "  PASS  refused_neighbour r=0.9 was deterministic across $REPLY_RUNS runs, as it should be"
  else
    print "  FAIL  refused_neighbour r=0.9 gave $REPLY_SPREAD distinct meshes -- it has been 3/3 and 6/6 identical; the reader is inventing differences"
    (( fails++ ))
  fi

  # The exporter check, on a case where the OFF is known to lose a real
  # distinction: two vertices 5.7e-7 mm apart print identically at six
  # significant figures. Two things must both hold -- the loss must be SEEN,
  # and the verdict must be UNCHANGED. That pair is the whole finding about the
  # OFF exporter, and if either half stops holding the sweep needs re-reading.
  print "the OFF exporter loses precision; it must be seen, and must not change the verdict"
  run_one refused_neighbour def 0.8
  cmp_out=$(python3 mesh.py --compare --tol $TOL \
      $WORK/refused_neighbour_fndef_r0.8.off $WORK/refused_neighbour_fndef_r0.8.stl 2>&1)
  if [[ $cmp_out == *"vertex counts differ"* ]]; then
    print "  PASS  refused_neighbour r=0.8: the OFF loses a vertex and the harness sees it"
  else
    print "  FAIL  refused_neighbour r=0.8: the known precision loss was not detected -- the comparison is not wired up"
    (( fails++ ))
  fi
  if [[ $REPLY_AGREE == yes ]]; then
    print "  PASS  refused_neighbour r=0.8: OFF and STL still reach the same verdict"
  else
    print "  FAIL  refused_neighbour r=0.8: OFF and STL now disagree on the VERDICT (agree=$REPLY_AGREE) -- the exporter has started changing answers; re-read the sweep"
    (( fails++ ))
  fi

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
    run_one selfproof $f def "ROT=0"; s0="$REPLY_VALID v=$REPLY_V e=$REPLY_E f=$REPLY_F chi=$REPLY_CHI nonman=$REPLY_NONMAN nmvert=$REPLY_NMVERT comp=$REPLY_COMP"
    run_one selfproof $f def "ROT=1"; s1="$REPLY_VALID v=$REPLY_V e=$REPLY_E f=$REPLY_F chi=$REPLY_CHI nonman=$REPLY_NONMAN nmvert=$REPLY_NMVERT comp=$REPLY_COMP"
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

print "\ndone. $(( $(wc -l < $OUT) - 1 )) rows in $OUT, weld tolerance $TOL, $REPEAT runs per cell"
print "invalid rows:"
awk -F'\t' 'NR>1 && $4 != "VALID" {print "  " $1 "  fn=" $2 "  r=" $3 "  " $4 "  nonman=" $5 "  nmvert=" $19 "  chi=" $6 "  comp=" $8 "/" $21 "  warn=" $12 "  tol=" $13 "  runs=" $14 "  distinct=" $15}' $OUT
print "rows whose fault is a pinched vertex (invisible to every edge count):"
awk -F'\t' 'NR>1 && $19 != "0" && $19 != "-" && $19 != "" {print "  " $1 "  fn=" $2 "  r=" $3 "  nmvert=" $19 "  chi=" $6}' $OUT
print "rows with more components than the model builds (a shed fragment):"
awk -F'\t' 'NR>1 && $21 != "" && $8 != "-" && $8+0 > $21+0 {print "  " $1 "  fn=" $2 "  r=" $3 "  comp=" $8 "  wanted " $21}' $OUT
print "binaries present in $OUT (more than one means the table mixes builds):"
# Field 18, not 17: the row is model fn r valid nonman chi genus comp v e f warn
# tol runs distinct agree secs bin, and 17 is the elapsed seconds -- which made
# this line report one "binary" per distinct render time, i.e. always several.
awk -F'\t' 'NR>1 {print $18}' $OUT | sort -u | sed 's/^/  /'
print "cells where the OFF export changed the answer (the exporter, not the geometry):"
awk -F'\t' 'NR>1 && $16 == "NO" {print "  " $1 "  fn=" $2 "  r=" $3}' $OUT
print "cells where genus is not 0 (a fillet that changed the topology), with the throat it turns on:"
awk -F'\t' 'NR>1 && $7 != "0" && $7 != "n/a" && $7 != "-" {print "  " $1 "  fn=" $2 "  r=" $3 "  genus=" $7 "  chi=" $6 "  throat" ($20=="-" ? " survives every weld tried" : "<=" $20 "mm")}' $OUT
print "cells whose runs disagreed with each other:"
awk -F'\t' 'NR>1 && $15 > 1 {print "  " $1 "  fn=" $2 "  r=" $3 "  distinct=" $15 " of " $14 " runs"}' $OUT
