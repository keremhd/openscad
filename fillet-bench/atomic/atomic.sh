#!/bin/zsh
# atomic.sh -- the atomic bench. One feature per case, aimed at from a camera the
# case itself carries, under each operation (original / fillet / chamfer) and each
# tessellation, with its dispatch expectation ASSERTED from the very render that
# made the picture.
#
#   ./atomic.sh                          everything: all cases, all pages, TSV, PDF
#   ./atomic.sh AC07                     one case, back into its own page
#   ./atomic.sh AC07-R3-C2               one tile, re-rendered in place
#   ./atomic.sh --family mixed-corner    substring filter on atomic-family
#   ./atomic.sh --class saddle           only the cases declaring that class
#   ./atomic.sh --check                  assertions only: no PNG, no montage
#   ./atomic.sh AC07 --accept "<reason>" re-stamp AC07's drifted expectations
#   ./atomic.sh --selftest               known answers only, before anything is believed
#   ./atomic.sh --no-pdf                 skip the bundle
#   ./atomic.sh --keep-stl               keep every STL, not only the failing ones
#
# Nothing here forces a dispatch class. Each case DECLARES what it was built to
# produce and the runner checks that against the `corners ...` echo of the render
# that made the tile; the construction pages are then a REGROUPING of tiles the run
# already made, so a construction nothing reaches is a reported coverage gap rather
# than a reason to add a knob to the operator.
set -u
cd "${0:A:h}"
zmodload zsh/mathfunc   # sqrt(), for turning the eye direction into a unit vector

# printf writes "36,616" under a comma-decimal locale, --camera reads that as two
# extra fields, and the render silently produces NO FILE. Pin the numeric locale.
export LC_ALL=C LC_NUMERIC=C

BIN=${BIN:-../../build/OpenSCAD.app/Contents/MacOS/OpenSCAD}
FLAGS=${FLAGS:---enable=fillet}
TOL=${TOL:-1e-6}
REPEAT=${REPEAT:-1}
TILE_W=${TILE_W:-520}
TILE_H=${TILE_H:-420}
FONT=${FONT:-/System/Library/Fonts/Helvetica.ttc}
FN_LADDER=(stock 8 19 32 64)
COUNTERS=(tube capTri cap coons saddle flat fan weld none)
# `none` is not a construction: it is the tally of corners that produced NOTHING,
# and the catch-all already fails any case that grows one. Requiring a case to
# reach it would be requiring the bench to carry a known defect on purpose, so it
# is the one counter with no coverage obligation.
CONSTRUCTIONS=(tube capTri cap coons saddle flat fan weld)
# The label a counter line gets is its highest-preference nonzero field. weld and
# none come last on purpose: they are what a corner does when no construction ran.
PREF=(tube capTri cap coons saddle flat fan weld none)
OPNAME=(original fillet chamfer)
OUT=results/atomic.tsv
LOG=results/expect-changes.log

mkdir -p cases tiles pages results selftest

# --- argument parsing ---------------------------------------------------------
ONLY_CASE=""; ONLY_TILE=""; FAMILY=""; CLASSF=""
CHECK=""; NOPDF=""; KEEPSTL=""; SELFTEST=""; ACCEPT=""; ACCEPT_REASON=""
while (( $# )); do
  case $1 in
    --check)     CHECK=1 ;;
    --no-pdf)    NOPDF=1 ;;
    --keep-stl)  KEEPSTL=1 ;;
    --selftest)  SELFTEST=1 ;;
    --repeat)    shift; REPEAT=$1 ;;
    --family)    shift; FAMILY=$1 ;;
    --class)     shift; CLASSF=$1 ;;
    --accept)    shift; ACCEPT=1; ACCEPT_REASON=${1:-} ;;
    -*)          print -u2 "unknown flag: $1"; exit 2 ;;
    *-R*-C*)     ONLY_TILE=$1; ONLY_CASE=${1%%-R*} ;;
    *)           ONLY_CASE=$1 ;;
  esac
  shift
done

[[ -x $BIN ]] || { print -u2 "no binary at $BIN -- set BIN="; exit 1; }
BINMD5=$(md5 -q $BIN)
BINSHORT=${BINMD5[1,8]}
STALE=$(find ../../src -newer $BIN -name '*.cc' -o -newer $BIN -name '*.h' 2>/dev/null | head -3)

banner() {
  print "binary: $BIN"
  print "        built $(stat -f '%Sm' $BIN)  md5 $BINSHORT"
  print "flags:  $FLAGS   weld tol $TOL   tile ${TILE_W}x${TILE_H}"
  if [[ -n $STALE ]]; then
    print "        STALE: src/ is newer than this binary --"
    print "$STALE" | sed 's|^|          |'
  fi
  print ""
}

# --- header access ------------------------------------------------------------
# All values are read from the case file's own `// atomic-<key>:` comments, the
# same way sheet.sh already reads `mesh.py-comp`.
hdr()  { sed -n "s|^// *atomic-$1: *||p" $CASEFILE }
hdr1() { hdr "$1" | head -1 }

# "cx,cy,cz | dx,dy,dz | dist" -> "ex,ey,ez,cx,cy,cz"
camera_of() {
  local spec=${1// /} centre dir dist cx cy cz dx dy dz len
  centre=${spec%%\|*}; spec=${spec#*\|}
  dir=${spec%%\|*};    dist=${spec#*\|}
  cx=${centre%%,*}; centre=${centre#*,}; cy=${centre%%,*}; cz=${centre#*,}
  dx=${dir%%,*};    dir=${dir#*,};       dy=${dir%%,*};    dz=${dir#*,}
  len=$(( sqrt(dx * dx + dy * dy + dz * dz) ))
  printf '%.3f,%.3f,%.3f,%s,%s,%s' \
    $(( cx + dx / len * dist )) $(( cy + dy / len * dist )) $(( cz + dz / len * dist )) \
    $cx $cy $cz
}

# --- the expectation engine ---------------------------------------------------
# A selector is `fn=<stock|N|*> op=<fillet|chamfer|*>`; the most specific line
# wins, and two lines of equal specificity matching one cell are a case-file
# error rather than a silent pick.
# resolve_get reports through two globals rather than through a pipeline, so that
# AMBIGUOUS survives: a subshell's variables would not.
RESOLVED=""; RESOLVED_AMBIG=""
resolve_get() {       # key fn op -> RESOLVED (body of the best line), RESOLVED_AMBIG
  local key=$1 fn=$2 op=$3
  local line sfn sop rest score best=-1 nbest=0
  local -a w
  RESOLVED=""; RESOLVED_AMBIG=""
  local -a lines
  lines=("${(@f)$(hdr $key)}")
  for line in $lines; do
    [[ -n $line ]] || continue
    w=(${=line})
    [[ ${w[1]} == fn=* && ${w[2]} == op=* ]] || continue
    sfn=${w[1]#fn=}; sop=${w[2]#op=}
    rest=${(j: :)w[3,-1]}
    [[ $sfn == '*' || $sfn == $fn ]] || continue
    [[ $sop == '*' || $sop == $op ]] || continue
    score=0
    [[ $sfn != '*' ]] && score=$((score + 2))
    [[ $sop != '*' ]] && score=$((score + 1))
    if   (( score > best )); then best=$score; RESOLVED=$rest; nbest=1
    elif (( score == best )); then nbest=$((nbest + 1)); fi
  done
  (( nbest > 1 )) && RESOLVED_AMBIG=1
  print -r -- "$RESOLVED"
}

label_from() {        # "tube=0 capTri=10 ..." -> the highest-preference nonzero field
  local got=$1 k v
  for k in $PREF; do
    v=$(print -r -- "$got" | sed -n "s|.*[^A-Za-z]$k=\([0-9][0-9]*\).*|\1|p")
    [[ -z $v ]] && v=$(print -r -- "$got" | sed -n "s|^$k=\([0-9][0-9]*\).*|\1|p")
    [[ -n $v && $v != 0 ]] && { print $k; return }
  done
  print none
}

# want -> OK, or "want: <want> / got: <got>"
assert_counters() {
  local want=$1 got=$2 tok k rel v g catchall="0" named=() bad=()
  typeset -A G
  local kv
  for kv in ${=got}; do
    [[ $kv == *=* ]] && G[${kv%%=*}]=${kv#*=}
  done
  for tok in ${=want}; do
    [[ $tok == '*='* ]] && { catchall=${tok#*=}; continue }
    [[ $tok == *=* || $tok == *'>='* || $tok == *'<='* ]] || continue
    if   [[ $tok == *'>='* ]]; then k=${tok%%>=*}; rel='>='; v=${tok#*>=}
    elif [[ $tok == *'<='* ]]; then k=${tok%%<=*}; rel='<='; v=${tok#*<=}
    else                            k=${tok%%=*};  rel='=';  v=${tok#*=}
    fi
    (( ${+G[$k]} )) || { bad+=("$k missing from the echo"); continue }
    named+=($k)
    g=${G[$k]}
    [[ $v == '*' ]] && continue
    case $rel in
      '=')  (( g == v )) || bad+=("$k=$g want $k=$v") ;;
      '>=') (( g >= v )) || bad+=("$k=$g want $k>=$v") ;;
      '<=') (( g <= v )) || bad+=("$k=$g want $k<=$v") ;;
    esac
  done
  if [[ $catchall != '*' ]]; then
    for k in $COUNTERS; do
      [[ " $named " == *" $k "* ]] && continue
      (( ${+G[$k]} )) || continue
      (( G[$k] == catchall )) || bad+=("$k=${G[$k]} want $k=$catchall (catch-all)")
    done
  fi
  if (( ${#bad} )); then
    print -r -- "want: $want / got: ${(j:, :)bad}"
  else
    print OK
  fi
}

# "VALID folds=0 warn=0 [reason]" against the measured triple
assert_mesh() {
  local want=$1 verdict=$2 folds=$3 warn=$4 tok k rel v cur bad=()
  local wv=${want%% *}
  [[ -z $want ]] && { print OK; return }
  [[ $verdict == $wv* ]] || bad+=("verdict=$verdict want $wv")
  for tok in ${=want}; do
    case $tok in
      folds*|warn*) ;;
      *) continue ;;
    esac
    if   [[ $tok == *'>='* ]]; then k=${tok%%>=*}; rel='>='; v=${tok#*>=}
    elif [[ $tok == *'<='* ]]; then k=${tok%%<=*}; rel='<='; v=${tok#*<=}
    elif [[ $tok == *=* ]];    then k=${tok%%=*};  rel='=';  v=${tok#*=}
    else continue; fi
    [[ $k == folds ]] && cur=$folds || cur=$warn
    [[ $v == '*' ]] && continue
    [[ $cur == <-> ]] || cur=0
    case $rel in
      '=')  (( cur == v )) || bad+=("$k=$cur want $k=$v") ;;
      '>=') (( cur >= v )) || bad+=("$k=$cur want $k>=$v") ;;
      '<=') (( cur <= v )) || bad+=("$k=$cur want $k<=$v") ;;
    esac
  done
  (( ${#bad} )) && print -r -- "${(j:, :)bad}" || print OK
}

# --- one cell -----------------------------------------------------------------
typeset -a FINDINGS
typeset -A SEEN_CLASS
FATAL=0

cell() {              # $CASEFILE $CASE $cam already set;  cell <row> <fn> <col>
  local row=$1 fn=$2 col=$3
  local op=$((col - 1)) opn=$OPNAME[$col]
  local id=$CASE-R$row-C$col
  local png=tiles/$id.png stl=tiles/$id.stl log=tiles/$id.log
  local args=(-D OP=$op)
  [[ $fn != stock ]] && args+=(-D FNSET=$fn)
  [[ -n $EXTRA && $col != 1 ]] && args+=(-D $EXTRA)

  local t0=$EPOCHREALTIME
  local -a outs
  if [[ -n $CHECK ]]; then outs=(-o $stl); else outs=(-o $png -o $stl); fi
  local runs=0 sigs=()
  while (( runs < REPEAT )); do
    ${=BIN} $=FLAGS --backend=manifold --render \
        --imgsize=$TILE_W,$TILE_H --projection=p --colorscheme=Cornfield \
        --camera=$CAM $args $outs $CASEFILE > $log 2>&1
    runs=$((runs + 1))
    [[ -f $stl ]] && sigs+=($(md5 -q $stl))
  done
  local secs=$(printf '%.2f' $(( EPOCHREALTIME - t0 )))
  local distinct=$(print -l $sigs | sort -u | wc -l | tr -d ' ')

  local mesh verdict bnd nonman nmvert chi genus comp v e f throat
  if [[ -f $stl ]]; then
    mesh=$(python3 ../mesh.py --tol $TOL --comp $WANTCOMP $stl 2>&1 | sed "s|^.*$stl: ||")
    folds=$(python3 ../folds.py --tol $TOL $stl 2>/dev/null | awk 'NR==1{print $2}')
  else
    mesh="NO MESH"; folds="-"
  fi
  [[ $folds == <-> ]] || folds=0
  verdict=${mesh%% *}
  fieldof() { print -r -- "$mesh" | sed -n "s|.*[[:space:]]$1=\([^[:space:]]*\).*|\1|p" }
  v=$(fieldof v); e=$(fieldof e); f=$(fieldof f); comp=$(fieldof comp)
  bnd=$(fieldof bnd); nonman=$(fieldof nonman); nmvert=$(fieldof nmvert)
  chi=$(fieldof chi); genus=$(fieldof genus)

  local corners tier warn class
  corners=$(sed -nE 's|^ECHO: (fillet\|chamfer): corners (.*) in file.*|\2|p' $log | sort -u | paste -sd'/' -)
  tier=$(sed -nE 's|^ECHO: (fillet\|chamfer): kept (.*) in file.*|\2|p' $log | sort -u | head -1)
  warn=$(grep -c 'WARNING:' $log); warn=${warn:-0}
  if [[ -n $corners ]]; then class=$(label_from "$corners"); else class="-"; fi

  # ---- THE CHECK THE WHOLE DATASET IS FOR ----
  local -a flags
  local want wt wm r
  if (( col > 1 )); then
    resolve_get expect $fn $opn >/dev/null; want=$RESOLVED
    [[ -n $RESOLVED_AMBIG ]] && flags+=("AMBIGUOUS")
    if [[ -z $want ]]; then
      flags+=("UNDECLARED")
    elif [[ $want == *noblend* ]]; then
      # `noblend` is the only way to declare "the operator refused the whole model
      # and echoed no corners line at all" -- a cell that starts blending again is
      # then a reported drift rather than a silent improvement.
      [[ -z $corners ]] || flags+=("DISPATCH DRIFT want: noblend / got: $corners")
    elif [[ -z $corners ]]; then
      flags+=("DISPATCH DRIFT want: $want / got: no corners echo at all")
    else
      r=$(assert_counters "$want" "$corners")
      [[ $r == OK ]] || flags+=("DISPATCH DRIFT $r")
      [[ $want == *'*=*'* ]] && flags+=(LOOSE)
    fi
    resolve_get tier $fn $opn >/dev/null; wt=$RESOLVED
    if [[ -n $wt && $tier != *$wt* ]]; then flags+=("TIER DRIFT want=$wt got=$tier"); fi
  fi
  resolve_get mesh $fn $opn >/dev/null; wm=$RESOLVED
  [[ -z $wm ]] && wm="VALID folds=0 warn=0"
  r=$(assert_mesh "$wm" "$verdict" "$folds" "$warn")
  [[ $r == OK ]] || flags+=("MESH DRIFT $r")
  [[ -n $(hdr1 measured) ]] || flags+=(UNSTAMPED)

  local flagstr=${(j:; :)flags}
  [[ -z $flagstr ]] && flagstr=OK
  local fatal=0
  [[ $flagstr == *DRIFT* || $flagstr == *UNDECLARED* || $flagstr == *AMBIGUOUS* ]] && fatal=1
  (( distinct > 1 )) && { flagstr="$flagstr; RUNS DISAGREE"; fatal=1 }
  (( fatal )) && FATAL=1
  [[ $flagstr != OK ]] && FINDINGS+=("$id  $CASE  fn=$fn  $opn  ::  $flagstr")

  # Coverage and the construction pages group by EVERY construction the cell
  # actually used, not only by the label: AC11 is tube AND capTri at one corner
  # vertex, and a page that showed it only under `tube` would report a coverage
  # gap on capTri that the run had in fact closed.
  if [[ -n $corners ]]; then
    local kk vv
    for kk in $COUNTERS; do
      vv=$(print -r -- " $corners" | sed -n "s|.*[^A-Za-z]$kk=\([0-9][0-9]*\).*|\1|p" | head -1)
      [[ -n $vv && $vv != 0 ]] && SEEN_CLASS[$kk]="${SEEN_CLASS[$kk]:-} $id"
    done
  fi

  print -- "$id\t$CASE\t$FAMILY_V\t$fn\t$col\t$opn\t$verdict\t$bnd\t$nonman\t$nmvert\t$chi\t$genus\t$comp\t$WANTCOMP\t$v\t$e\t$f\t$folds\t$warn\t$corners\t$class\t$tier\t$WANTCLASS\t$flagstr\t$TOL\t$runs\t$distinct\t$secs\t$BINSHORT\t$CAM" >> $OUT
  print -r -- "$id  fn=$fn $opn  $class  ${corners:--}  $verdict folds=$folds warn=$warn  [$flagstr]"

  if [[ -z $CHECK ]]; then
    local col3
    [[ $flagstr == OK ]] && col3=gray30 || col3=red
    if [[ -f $png ]]; then
      magick $png -background white -bordercolor white -border 6 \
        -font $FONT -pointsize 19 -fill black \
        label:"$id  $CASE  fn=$fn  $opn" -gravity center -append \
        -font $FONT -pointsize 14 -fill gray30 \
        label:"$class   folds=$folds   ${corners:--}" -gravity center -append \
        -font $FONT -pointsize 15 -fill $col3 \
        label:"$verdict  warnings=$warn  $flagstr" -gravity center -append \
        tiles/$id-labeled.png 2>/dev/null
    else
      print -u2 "$id: NO OUTPUT -- see $log"
      magick -size ${TILE_W}x${TILE_H} xc:white -bordercolor white -border 6 \
        -font $FONT -pointsize 19 -fill red \
        label:"$id  $CASE  fn=$fn  $opn" -gravity center -append \
        -font $FONT -pointsize 15 -fill red \
        label:"NO IMAGE  $verdict  warnings=$warn  $flagstr" -gravity center -append \
        tiles/$id-labeled.png 2>/dev/null
    fi
  fi

  if [[ -z $KEEPSTL && $verdict == VALID && $flagstr == OK ]]; then rm -f $stl; fi
}

# --- selftest -----------------------------------------------------------------
selftest() {
  local fails=0
  print -r -- "-- mesh.py / folds.py known answers"
  python3 ../mesh.py  --selftest --tol $TOL | tail -1
  python3 ../mesh.py  --selftest --tol $TOL > /dev/null || fails=$((fails + 1))
  python3 ../folds.py --selftest --tol $TOL | tail -1
  python3 ../folds.py --selftest --tol $TOL > /dev/null || fails=$((fails + 1))

  print "";   print -r -- "-- -D OP and -D FNSET demonstrably reach the model"
  local a b
  a=$(${=BIN} $=FLAGS --backend=manifold --render -D OP=0 -o /tmp/at_a.stl cases/AC01-crease-concave.scad 2>&1 | grep -c .)
  ${=BIN} $=FLAGS --backend=manifold --render -D OP=0 -o /tmp/at_a.stl cases/AC01-crease-concave.scad >/dev/null 2>&1
  ${=BIN} $=FLAGS --backend=manifold --render -D OP=1 -o /tmp/at_b.stl cases/AC01-crease-concave.scad >/dev/null 2>&1
  a=$(grep -c 'facet normal' /tmp/at_a.stl); b=$(grep -c 'facet normal' /tmp/at_b.stl)
  if [[ $a != $b ]]; then print "  PASS  -D OP changes the solid ($a vs $b facets)"
  else print "  FAIL  -D OP did not reach the model ($a facets both ways)"; fails=$((fails + 1)); fi
  ${=BIN} $=FLAGS --backend=manifold --render -D OP=1 -D FNSET=8  -o /tmp/at_a.stl cases/AC06-elbow-square.scad >/dev/null 2>&1
  ${=BIN} $=FLAGS --backend=manifold --render -D OP=1 -D FNSET=64 -o /tmp/at_b.stl cases/AC06-elbow-square.scad >/dev/null 2>&1
  a=$(grep -c 'facet normal' /tmp/at_a.stl); b=$(grep -c 'facet normal' /tmp/at_b.stl)
  if [[ $a != $b ]]; then print "  PASS  -D FNSET changes the solid ($a vs $b facets)"
  else print "  FAIL  -D FNSET did not reach the model ($a facets both ways)"; fails=$((fails + 1)); fi

  print "";   print -r -- "-- the expectation engine, in both directions"
  local got="tube=2 capTri=10 cap=0 coons=0 saddle=0 flat=0 fan=0 weld=16 none=0"
  local -a probes
  probes=(
    "tube=2 capTri=10 weld=16 *=0|OK|an exact line that matches"
    "saddle=2 *=0|FAIL|a deliberately wrong class"
    "tube=2 *=0|FAIL|the catch-all catches capTri nobody named"
    "tube=2 *=*|OK|the loose catch-all tolerates it"
    "tube>=1 weld<=20 *=*|OK|the range forms"
    "tube<=1 *=*|FAIL|the range forms, failing"
    "capTri=* tube=2 weld=16 *=0|OK|an explicit do-not-care"
  )
  local p wantr r desc
  for p in $probes; do
    wantr=${${p#*|}%%|*}; desc=${p##*|}; p=${p%%|*}
    r=$(assert_counters "$p" "$got")
    if [[ ( $wantr == OK && $r == OK ) || ( $wantr == FAIL && $r != OK ) ]]; then
      print "  PASS  $desc"
    else
      print "  FAIL  $desc -> $r"; fails=$((fails + 1))
    fi
  done

  print "";   print -r -- "-- selector resolution"
  local f
  for f in selftest/SF*.scad; do
    CASEFILE=$f
    local nm=${f:t:r}; local r2=""
    case $nm in
      SFspecific)
        resolve_get expect stock fillet >/dev/null; r2=$RESOLVED
        [[ $r2 == *'saddle=2'* ]] && print "  PASS  most specific wins ($r2)" \
                                  || { print "  FAIL  most specific lost ($r2)"; fails=$((fails + 1)) } ;;
      SFambiguous)
        resolve_get expect stock fillet >/dev/null; r2=$RESOLVED
        [[ -n $RESOLVED_AMBIG ]] && print "  PASS  AMBIGUOUS fires" \
                                 || { print "  FAIL  AMBIGUOUS did not fire"; fails=$((fails + 1)) } ;;
      SFundeclared)
        resolve_get expect stock fillet >/dev/null; r2=$RESOLVED
        [[ -z $r2 ]] && print "  PASS  UNDECLARED fires at stock" \
                     || { print "  FAIL  something matched stock ($r2)"; fails=$((fails + 1)) }
        resolve_get expect 8 fillet >/dev/null; r2=$RESOLVED
        [[ -n $r2 ]] && print "  PASS  the declared row still resolves" \
                     || { print "  FAIL  the declared row did not resolve"; fails=$((fails + 1)) } ;;
    esac
  done

  print "";   print -r -- "-- end to end, on a real render"
  local savedcheck=$CHECK savedout=$OUT
  CHECK=1; OUT=/dev/null
  for f in selftest/SFright-*.scad selftest/SFwrong-*.scad; do
    CASEFILE=$f; CASE=${${f:t:r}%%-*}
    FAMILY_V=$(hdr1 family); WANTCLASS=$(hdr1 class); EXTRA=$(hdr1 extra)
    WANTCOMP=$(sed -n 's|^// *mesh\.py-comp: *||p' $CASEFILE | head -1); : ${WANTCOMP:=1}
    CAM=$(camera_of "$(hdr1 camera)")
    local before=${#FINDINGS}
    cell 1 stock 2 > /dev/null
    local after=${#FINDINGS}
    if [[ $CASE == SFright ]]; then
      (( after == before )) && print "  PASS  the correct fixture reports OK" \
                            || { print "  FAIL  the correct fixture flagged: $FINDINGS[-1]"; fails=$((fails + 1)) }
    else
      if (( after > before )) && [[ $FINDINGS[-1] == *"DISPATCH DRIFT"* ]]; then
        print "  PASS  the wrong fixture reports DISPATCH DRIFT"
      else
        print "  FAIL  the wrong fixture passed silently"; fails=$((fails + 1))
      fi
    fi
  done
  FINDINGS=(); FATAL=0
  CHECK=$savedcheck; OUT=$savedout

  print "";   print -r -- "-- the locale trap"
  CASEFILE=cases/AC01-crease-concave.scad
  local c=""; c=$(camera_of "$(hdr1 camera)")
  if [[ $c == *,*,*,*,*,* && $(print -r -- "$c" | awk -F, '{print NF}') == 6 ]]; then
    print "  PASS  the computed camera has exactly six fields ($c)"
  else
    print "  FAIL  the camera string is not six fields ($c)"; fails=$((fails + 1))
  fi

  print ""
  if (( fails )); then print "SELFTEST FAILED: $fails check(s)"; return 1; fi
  print "SELFTEST PASSED."
  return 0
}

# --- --accept -----------------------------------------------------------------
accept() {
  [[ -n $ACCEPT_REASON ]] || { print -u2 "--accept needs a reason string"; exit 2 }
  [[ -n $ONLY_CASE ]]     || { print -u2 "--accept needs a case id"; exit 2 }
  if [[ -n $STALE ]]; then
    print -u2 "--accept refuses: src/ is newer than $BIN. The numbers being"
    print -u2 "accepted are about a build nobody has. Rebuild, re-run, then accept."
    exit 2
  fi
  if [[ -n $(git -C ../.. status --porcelain -- ../../src 2>/dev/null) ]]; then
    print -u2 "--accept refuses: src/ has uncommitted changes."
    exit 2
  fi
  print -u2 "--accept: this run does not rewrite case files automatically."
  print -u2 "It re-stamps atomic-measured and logs the reason; edit the expectation"
  print -u2 "line yourself from the measured 'got:' in the summary above, so the"
  print -u2 "number that lands in the file is one a person read."
  local f d=$(date +%Y-%m-%d)
  for f in cases/$ONLY_CASE*.scad; do
    print "$(date '+%Y-%m-%d %H:%M:%S')\t${f:t}\tstamp\t$BINSHORT\t$ACCEPT_REASON" >> $LOG
    sed -i '' "s|^// atomic-measured: .*|// atomic-measured:  $BINSHORT $d|" $f
    print "restamped ${f:t} -> $BINSHORT $d"
  done
  exit 0
}

# --- main ---------------------------------------------------------------------
zmodload zsh/datetime
banner
[[ -n $SELFTEST ]] && { selftest; exit $? }
[[ -n $ACCEPT ]] && accept

if [[ -z $ONLY_CASE && -z $FAMILY && -z $CLASSF ]]; then
  print "# tile\tcase\tfamily\tfn\tcol\top\tvalid\tbnd\tnonman\tnmvert\tchi\tgenus\tcomp\twantcomp\tv\te\tf\tfolds\twarn\tcorners\tclass\ttier\twant_class\tverdict_flags\ttol\truns\tdistinct\tsecs\tbin\tcamera" > $OUT
elif [[ ! -f $OUT ]]; then
  print "# tile\tcase\tfamily\tfn\tcol\top\tvalid\tbnd\tnonman\tnmvert\tchi\tgenus\tcomp\twantcomp\tv\te\tf\tfolds\twarn\tcorners\tclass\ttier\twant_class\tverdict_flags\ttol\truns\tdistinct\tsecs\tbin\tcamera" > $OUT
fi

typeset -a DONE_CASES
T0=$EPOCHREALTIME
for CASEFILE in cases/*.scad; do
  CASE=$(hdr1 case)
  [[ -n $CASE ]] || { print -u2 "${CASEFILE:t}: no atomic-case: header, skipped"; continue }
  [[ -n $ONLY_CASE && $ONLY_CASE != $CASE ]] && continue
  FAMILY_V=$(hdr1 family)
  WANTCLASS=$(hdr1 class)
  EXTRA=$(hdr1 extra)
  [[ -n $FAMILY && $FAMILY_V != *$FAMILY* ]] && continue
  [[ -n $CLASSF && $WANTCLASS != $CLASSF ]] && continue
  WANTCOMP=$(sed -n 's|^// *mesh\.py-comp: *||p' $CASEFILE | head -1); : ${WANTCOMP:=1}
  CAM=$(camera_of "$(hdr1 camera)")
  local_fns=$(hdr1 fn)
  if [[ -n $local_fns ]]; then fns=(${(s:,:)${local_fns// /}}); else fns=($FN_LADDER); fi

  print "\n== $CASE  $(hdr1 title)"
  print "   family=$FAMILY_V class=$WANTCLASS camera=$CAM ${EXTRA:+extra=$EXTRA}"
  r=0
  for fn in $fns; do
    r=$((r + 1))
    for c in 1 2 3; do
      [[ -n $ONLY_TILE && $ONLY_TILE != $CASE-R$r-C$c ]] && continue
      cell $r $fn $c
    done
  done
  DONE_CASES+=($CASE)

  if [[ -z $CHECK ]]; then
    files=(tiles/$CASE-R*-C*-labeled.png(Nn))
    if (( ${#files} )); then
      montage -background white -tile 3x${#fns} -geometry +8+8 $files pages/$CASE.png 2>/dev/null
      magick pages/$CASE.png -background white -font $FONT -pointsize 30 -fill black \
        label:"atomic bench -- $CASE: $(hdr1 title)" -gravity center +swap -append \
        -font $FONT -pointsize 17 -fill gray30 \
        label:"declared class $WANTCLASS   select=$(hdr1 select)   $(hdr1 params)   camera $CAM" \
        -gravity center -append pages/$CASE.png 2>/dev/null
      print "   -> pages/$CASE.png"
    fi
  fi
done
WALL=$(printf '%.1f' $(( EPOCHREALTIME - T0 )))

# --- the construction pages: NO new renders, regroup by MEASURED class ---------
typeset -a GAPS
if [[ -z $CHECK && -z $ONLY_CASE && -z $FAMILY && -z $CLASSF ]]; then
  print "\n== construction pages (regrouped from the tiles already made)"
  for k in $COUNTERS; do
    ids=(${=SEEN_CLASS[$k]:-})
    files=()
    for id in $ids; do
      [[ -f tiles/$id-labeled.png ]] && files+=(tiles/$id-labeled.png)
      (( ${#files} >= 12 )) && break
    done
    if (( ${#files} )); then
      montage -background white -tile 4x3 -geometry +8+8 $files pages/K-$k.png 2>/dev/null
      magick pages/K-$k.png -background white -font $FONT -pointsize 30 -fill black \
        label:"atomic bench -- every corner the '$k' construction actually closed (${#ids} cell(s) measured, ${#files} shown)" \
        -gravity center +swap -append pages/K-$k.png 2>/dev/null
      print "   K-$k: ${#ids} cell(s) -> pages/K-$k.png"
    else
      rm -f pages/K-$k.png
      [[ " $CONSTRUCTIONS " == *" $k "* ]] && GAPS+=($k)
      print "   K-$k: EMPTY"
    fi
  done
fi

# --- summary ------------------------------------------------------------------
print "\n================ summary ================"
print "cases: ${#DONE_CASES}   wall: ${WALL}s   binary: $BINSHORT   tol: $TOL"
if (( ${#FINDINGS} )); then
  print "\nflagged cells (${#FINDINGS}):"
  for l in $FINDINGS; do print "  $l"; done
else
  print "\nno flagged cells."
fi
if [[ -z $CHECK && -z $ONLY_CASE && -z $FAMILY && -z $CLASSF ]]; then
  print "\ncoverage (measured, not declared):"
  for k in $COUNTERS; do
    ids=(${=SEEN_CLASS[$k]:-})
    cs=($(print -l ${ids/-R*/} | sort -u))
    printf '  %-8s %3d cell(s)  %s\n' $k ${#ids} "${(j:,:)cs}"
  done
  for k in $GAPS; do
    print "  COVERAGE GAP: $k -- no case anywhere measured a nonzero $k"
    FATAL=1
  done
fi

if [[ -z $CHECK && -z $NOPDF && -z $ONLY_CASE ]]; then
  if [[ -x ../sheets-to-pdf.sh ]]; then
    ../sheets-to-pdf.sh --from 'atomic/pages/*.png' atomic/pages/atomic.pdf \
      || print -u2 "sheets-to-pdf.sh failed -- the page PNGs are still current"
  fi
fi

print "\nresults: $OUT"
(( FATAL )) && { print "ATOMIC: FAILED"; exit 1 }
print "ATOMIC: clean"
exit 0
