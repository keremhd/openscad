# The atomic bench

A second instrument beside `fillet-bench`, not a replacement for it.

Where the sheets in `../sheets/` render **whole models through one hardcoded
operation at one camera**, this set renders **one feature, from a camera aimed at
it, under each operation and each tessellation** — and **asserts, per case, that
the corner construction the case was built to exercise is the one that actually
ran**.

```sh
./atomic.sh                          everything: 26 cases, all pages, TSV, PDF
./atomic.sh AC07                     one case, back into its own page
./atomic.sh AC07-R3-C2               one tile, re-rendered in place
./atomic.sh --family mixed-corner    substring filter on atomic-family
./atomic.sh --class saddle           only the cases declaring that class
./atomic.sh --check                  assertions only: no PNG, no montage (the CI mode)
./atomic.sh --selftest               known answers only, before anything is believed
./atomic.sh AC07 --accept "<reason>" re-stamp AC07 after a deliberate change
./atomic.sh --no-pdf                 skip the bundle
./atomic.sh --keep-stl               keep every STL, not only the failing ones
```

`BIN`, `FLAGS`, `TOL`, `TILE_W`, `TILE_H`, `FONT`, `REPEAT` are environment
overrides with the same meanings as in `../sheet.sh` and `../sweep.sh`.

Run `./atomic.sh --selftest` before believing anything. It refuses the run unless
`mesh.py` and `folds.py` pass their own controls, `-D OP` and `-D FNSET`
demonstrably change the solid, the expectation engine reports `OK` on a correct
fixture **and `DISPATCH DRIFT` on a deliberately wrong one**, selector resolution
fires most-specific-wins / `AMBIGUOUS` / `UNDECLARED` exactly once each, and the
computed `--camera` string has six fields (a comma-decimal locale writes
`36,616`, `--camera` reads it as extra fields, and the render silently produces
**no file**).

## What is on a page

`pages/ACnn.png` is one case, 3 columns × 5 rows:

|            | original | `fillet(r = R)` | `chamfer(t = CT)` |
|---|---|---|---|
| `$fn` stock | `ACnn-R1-C1` | `ACnn-R1-C2` | `ACnn-R1-C3` |
| `$fn` 8     | `-R2-*` | | |
| `$fn` 19    | `-R3-*` | | |
| `$fn` 32    | `-R4-*` | | |
| `$fn` 64    | `-R5-*` | | |

Same camera in all fifteen tiles. Column 1 is re-rendered per row rather than
reused: three cases carry cylinders, so their *source* is `$fn`-dependent and a
shared column-1 tile would quietly lie about them.

The ladder is `stock, 8, 19, 32, 64`, each earned from the bench's record: stock
because the old corpus never rendered defaults; 8 because a coarse family is
invalid there and clean everywhere else; **19 because it is odd and the
odd-`$fn` parity defect is live** — and it turns out to matter for *dispatch*,
not only for folds (see the coons note below); 32 because `rib_into_boss` is
invalid at 32 and valid either side; 64 as the fine limit. The `stock` row
passes **no** `-D` at all, which is the only honest way to ask for defaults.

`pages/K-<construction>.png` is the other axis: for each construction, every
column-2 and column-3 tile whose **measured** counter line used it, at most
twelve per page. Nothing is forced and nothing is re-rendered — the construction
pages are a **regrouping of tiles the run already made**, so a corner that
migrates from `coons` to `tube` leaves one page, appears on another, *and* trips
`DISPATCH DRIFT` on its own case. The picture and the assertion cannot disagree,
because both come from the same scraped line of the same log.

`pages/atomic.pdf` bundles every page through `../sheets-to-pdf.sh`, which now
takes `--from '<glob>'`. It sits **beside** `../sheets/sheets.pdf`, not inside
it.

## The case format

Every case is one `.scad` in `cases/`, carrying its own camera, its own
parameters and its own expectations in `// atomic-<key>:` header comments — read
with `sed`, exactly the way `../sheet.sh` already reads `mesh.py-comp`.

```
// atomic-case:      AC07
// atomic-title:     square elbow with the end face raked 40 degrees
// atomic-family:    mixed-corner
// atomic-class:     tube                     the construction this case EXISTS to exercise
// atomic-select:    both                     both | convex | concave
// atomic-camera:    6,6,6 | -1,-1,0.7 | 54   centre | eye direction | distance
// atomic-params:    R=2 CT=2
// atomic-fn:        stock,8,19,32,64         optional, overrides the ladder
// atomic-extra:     MINANG=25                optional, -D'd into columns 2 and 3 only
//
// atomic-expect:    fn=*  op=fillet   tube=2 capTri=10 weld=26 *=0
// atomic-tier:      fn=*  op=fillet   tier=pullIn
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
//
// atomic-measured:  9de6d8c0 2026-08-21      the binary md5 these were taken on
// atomic-history:   what wrong looked like here, and why the case is shaped this way
// mesh.py-comp:     1
```

`atomic-camera` is `centre | direction | distance`, not a raw `--camera` string:
`../corner-sheet6.sh` already computes an eye from exactly this triple, re-aiming
is one number, and the resolved string still goes into the TSV literally. **The
centre is a vertex read off the model's own source** — the oblique cases write
`y = -x*tan(40) + 11.03` out in a comment rather than eyeballing it.

`atomic-class` is documentation and the key the construction pages are named by;
the binding check is `atomic-expect`.

### The expectation language

Selector: `fn=<stock|8|19|32|64|*>` and `op=<original|fillet|chamfer|*>`. **Most
specific wins**, scored `fn` exact + `op` exact > `fn` exact > `op` exact > both
`*`. Two lines of equal specificity matching one cell is a **case-file error**,
reported and fatal — an ambiguous expectation is worse than none.

Counter constraints, applied to the nine fields of the operator's own
`corners tube= capTri= cap= coons= saddle= flat= fan= weld= none=` echo:

| form | meaning |
|---|---|
| `saddle=2` | exact |
| `weld>=4` | at least |
| `fan<=2` | at most |
| `capTri=*` | present, any value — an explicit "we do not care", and it must be **written** to be tolerated |
| `*=0` | the **catch-all**: every counter not named on this line must be zero |
| `*=*` | every counter not named is unconstrained — allowed, but the cell is flagged `LOOSE` |
| `noblend` | the operator echoed **no** corners line at all: it refused the whole model |

`*=0` is the default when no catch-all is written, and every case in the
inventory uses it. That is what makes an exact line a *complete* statement about
the call: a corner that appears where none was is caught even if it appears in a
counter nobody thought about.

`atomic-mesh` is `<verdict> [folds<constraint>] [warn<constraint>] [reason]`;
anything after those tokens is a free-text reason and is ignored by the checker.
Column 1 falls back to `VALID folds=0 warn=0` when nothing matches it.

### Flags

| flag | meaning | full run |
|---|---|---|
| `OK` | every constraint satisfied | — |
| `DISPATCH DRIFT` | a counter constraint failed; prints want and got | **fail** |
| `TIER DRIFT` | `kept tier=` moved | **fail** |
| `MESH DRIFT` | verdict, folds or warnings moved from the declared line | **fail** |
| `UNDECLARED` | no expectation line matched this cell | **fail** |
| `AMBIGUOUS` | two equally specific lines matched | **fail** |
| `RUNS DISAGREE` | `--repeat N` produced more than one distinct STL | **fail** |
| `LOOSE` | the matched line used `*=*` | warn |
| `UNSTAMPED` | the case has no `atomic-measured` line | warn |
| `COVERAGE GAP: <k>` | no case anywhere measured a nonzero `<k>` | **fail** |

## Per-row expectations are first class

**`$fn` rows may carry different expectations, and this is the normal case, not
an escape hatch.** Dispatch legitimately moves with tessellation and this bench
has measured it repeatedly — AC08 is `capTri`+`flat`+`fan` at stock and grows a
`tube` at 19; AC12's rib termini are `fan` at stock, `coons` at 19 and `saddle`
at 32 and 64; AC21's sliver survives as a `saddle` at stock and is welded away
from 19 up. A design that allowed one expectation per case would either be wrong
on these or would have to weaken every one of them, so the selector carries `fn=`
and a case whose class legitimately shifts **declares the shift** — which turns
the shift into an asserted, diffable fact instead of a footnote.

The risk is the mirror: declaring `saddle` at `$fn = 8` "because that is what it
does" would hide a coarse-tessellation fault behind a green check. The mitigation
is social and is a rule of this directory: **a per-row expectation that differs
from its neighbours must carry a reason token on the line**, and those reasons
are what a reviewer reads first.

## How "one corner" is possible when `fillet()` takes a solid

Four levers. Every case says in its header which it used.

1. **The feature is the model.** A mixed corner needs two boxes; a convex
   trihedral corner needs one. Nothing in the inventory needs to be bigger.
2. **Sign filtering.** `convex = false` / `concave = false` deletes every corner
   of the wrong sign from the call. AC01 and AC04 use it. Cases that exist to
   show a *mixed* corner cannot, and say so.
3. **The brush.** `fillet(r) { part(); brush(); }` restricts selection to the
   edges the brush volume touches — AC02 isolates exactly one convex crease this
   way and AC23 uses it to leave the vertical rib/boss creases sharp. This is the
   lever the sign filters cannot give: `convex = true` on a plain box selects all
   twelve edges.
4. **`min_angle`**, raised to suppress selection or lowered to select a shallow
   fold, where the case is about the selection boundary itself (AC22 / AC22b).

The target is a counter line with **no more than three or four nonzero fields**.
A case whose line is longer than that is not atomic and should be split.

## Scale, and one trap in shrinking

**`R` stays at 2 in almost every case and only the solid shrinks.** The blend
arc's stock facet count reads the *blend* radius, so a fixed `R` gives every case
the same arc tessellation and makes the `$fn` rows one axis across cases.
Shrinking `R` instead moves each case to a different point of the `$fa`/`$fs`
crossover and the rows stop comparing. (AC13 and AC18 say in their headers why
they are the exceptions.)

**Shrink the long extents, never the thickness.** Dividing every extent of
`lbracket` by three puts its 6 mm plate at 2 mm against `2R = 4` and the operator
refuses — measured, on the first draft of AC06, and it is AC19's case, not
AC06's. The elbow family here keeps lbracket's own 6 mm plate and is only
shorter.

**Rakes must cut a face, not graze it.** A rake plane that leaves the near face
within a millimetre of a leaning wall's base leaves a sliver the operator refuses
— nine sharp creases and a warning. Three framings of AC10 were thrown away to
that before the rake was moved to exit at `x = 10`, between the crease at 6 and
the wall base at 12.93.

**The three curved cases are re-framed and never re-scaled.** A large-radius
cylinder at stock defaults puts a whole corner inside one 12° facet and quietly
becomes a plain oblique elbow; `elbow_curved_endface` had to be rebuilt at
r = 9.5 mm before its crease terminus was three facets wide. AC14, AC15 and AC23
carry the originals' dimensions for that reason.

## Auditing an expectation change

The bench README names this effort's recurring failure mode — *expectations kept
past their evidence*, and its mirror, *expectations edited until the run goes
green*. Three rules:

1. **The runner never edits a case file on its own.** Without `--accept`, a drift
   is a failure and nothing on disk changes.
2. **`--accept` takes a mandatory reason**, re-stamps `atomic-measured` with the
   binary's md5 prefix and the date, and appends to `results/expect-changes.log`.
   It deliberately does **not** rewrite the expectation line itself: the number
   that lands in the file has to be one a person read out of the `got:` in the
   summary.
3. **`--accept` refuses on a stale binary or a dirty `src/`.** If `find ../../src
   -newer $BIN` is non-empty, the numbers being accepted are about a build nobody
   has. The banner prints the same check on every run.

Reviewing a behaviour change is then the diff of `results/atomic.tsv` beside the
diff of two page PNGs.

## `results/atomic.tsv`

Tab-separated, one row per cell, **new columns appended at the end forever** —
`sweep.sh`'s rule, because a column inserted in the middle silently reassigns
every field index in every recorded file.

```
tile case family fn col op valid bnd nonman nmvert chi genus comp wantcomp
v e f folds warn corners class tier want_class verdict_flags tol runs distinct
secs bin camera
```

`corners` is the literal echo; `class` is the label derived from it (the
highest-preference nonzero counter); `verdict_flags` is **the column to diff** —
"algorithm X regressed on corner class Y" is a token flip on a named row. `bin`
is the binary's md5 prefix, not its mtime, because a rebuild of identical bytes
is the same instrument and a copy is not a different one.

## What the first full run found

Written down here because it is the kind of thing that is lost the moment it is
fixed. Every one of these is now an asserted line in the case that found it.

- **`coons` exists, and it is an odd-row phenomenon.** No case in the inventory
  reaches the Coons membrane at stock. AC10b reaches it at `$fn = 19`, AC12 at
  19, AC17 at 19 and 32, AC20 at 32 — and every one of them is back on `tube`,
  `saddle` or `flat` at 64. The parity axis shows up in *dispatch*, not only in
  folds. AC10 — a 40° rake on a 120° trough, both gate violations at once —
  measures `tube` at every row, which is what the canal corner was landed to do.
- **`elbow_facet_endface` blends, and the `noblend` stamp was the instrument.**
  The first full run measured "13 open edge(s) … the model is returned
  unchanged" at every AC16 row and wrote the expectation as `noblend`. That run
  was taken on a transient mid-development binary that was never committed. On
  the committed binary AC16 blends at every row — `tube=1 capTri=10 coons=1
  weld=55` off the `fullMitre` tier, no warning — which is the number the
  bench's own record always had. The lesson kept here is about the stamp, not
  the operator: an `atomic-measured` line naming a build nobody can rebuild is
  not evidence, and the first thing to do with a drift against one is to re-run,
  not to explain.
- **Three equal arcs is not what `capTri` requires.** AC05 rakes the top face
  about both x and y so no two arcs at a corner are equal, and still measures
  `capTri=8` at every row. The inventory's real `cap` case is AC23, the brushed
  boss.
- **The sliver fuse has a row.** AC21's 0.05 mm step survives as a `saddle`
  plus a `fan` at stock and `$fn = 8` and is welded away from 19 up, taking two
  corners and two creases with it.
- **Chamfer has exactly two corner constructions**, `flat` and `fan`, because
  the ladder gates `capTri`, `cap`, `coons` and `saddle` behind `!isChamfer`.
  Column 3 shows that on every page for free, and no other page in the bench does.
  `CT = R` is a *visual* pairing, not a material-removal-equivalent one.
- **`none` acquired a meaning, and three cases now declare it.** It began as the
  tally of corners that produced nothing — a defect counter, excluded from the
  coverage requirement and caught by the `*=0` catch-all. The terminus bite gave
  it a second, legitimate population: a strip end whose cross-section the end
  face has taken into its own outline is *already closed*, and a patch there
  would be a second sheet lying on the face. So AC02 measures `none=2` where its
  concave twin AC01 still measures `flat=2`, AC17 `none=4`, and AC19 `none=10`
  where it used to close twelve flat caps. Those counts are written out
  explicitly rather than waved through with `*=*`, so a corner that starts
  producing nothing *for the old reason* is still a reported drift. `none` keeps
  its exemption from the coverage requirement: a bench must not be obliged to
  carry a known defect on purpose.
- **`flat` survives the bite.** With the terminus caps gone it would have been
  reasonable to expect `flat` to empty out; it did not. 95 cells across 20 cases
  still reach it, because every chamfer corner closes `flat` or `fan` and the
  fillet ladder still falls back to an ear clip when the Coons membrane will not
  take (AC17 at 32 and 64). No construction is uncovered.

## Relationship to the existing bench

Nothing is deleted on the strength of this directory.

| instrument | its question |
|---|---|
| sheets 1–4 (`../sheet.sh`) | is the whole model a valid solid at stock and forced `$fn` |
| sheet 5 (`../corner-sheet.sh`) | what does each dispatch class look like on a real model |
| sheet 6 (`../corner-sheet6.sh`) | where is the torus gate's edge |
| `../sweep.sh` | `$fn` × radius over the corpus |
| **`atomic/`** | which construction handles which feature, asserted per case, and what each does at the edge |

Sheet 5 and the construction pages both show dispatch classes; sheet 5 shows
which class a *real model* gets, the construction pages show every corner a class
*actually closed*.

## Standing use

`./atomic.sh --check` after every `FilletBlend.cc` change — about 90 seconds, no
images, exits nonzero on drift. The full run before a sheet regeneration. `diff`
on `results/atomic.tsv` and `results/expect-changes.log` at review.

Tile ids are stable while a case keeps its id, the `$fn` ladder keeps its order
and the column order holds. New cases are **appended** with new `ACnn` numbers,
never sorted in.
