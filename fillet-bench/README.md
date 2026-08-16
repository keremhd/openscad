# fillet-bench — the default-settings bench and contact sheets

The instrument the acceptance gate is measured with, and the one thing in this
effort designed to be *looked at* rather than reported.

```sh
./sheet.sh                  # render everything, rebuild all sheets
./sheet.sh S2-T07           # re-render one tile, drop it back into its sheet
./sheet.sh --models tee     # every tile of one model
./corner-sheet.sh           # just sheet 5, the zoomed corner-dispatch page
./corner-sheet6.sh          # just sheet 6, the torus-gate boundary page
open sheets/sheet-1.png
```

## Why it exists

The 225-model corpus every headline measurement in this effort cites **writes an
explicit `$fn` in every model, and no pass ever looked at a junction render.**
That is how three visible faults survived an effort that measured genus on 225
models.

The old harness is not lost, but it is not runnable either. `census.py`, `mesh2.py`,
`nonman.py`, `sweep3.py` and about fifty more are committed on `integ-three`,
`integ-all-four` and `audit-chain-verts` (`git show integ-three:work/census.py`),
along with ~35 base models. What is gone is their footing: `sweep3.py` hardcodes
absolute paths into a deleted worktree, and the seven comparison binaries it names
were never committed. The 225 rows were model × `$fn` × radius variants generated
from those base models rather than stored files, and the generator that assembles
the exact set has not been located.

So the old apparatus needs its paths repointed and its binaries rebuilt before it
can say anything. This bench needs neither.

This bench is the opposite of that by construction: **no model sets `$fn`**, every
model is rendered, and the render is the deliverable. It is also committed, so it
survives the worktree it was run in.

## The tile id

Every tile carries an id like `S2-T07` — sheet 2, slot 7 — printed under the
picture along with the model name, its validity and its warning count. The id is
stable while `expect.txt` keeps its order.

That is the whole point of the numbering: **"S2-T07's corner looks wrong" names one
render, one model and one set of arguments**, and `sheets/INDEX.md` resolves it to
all three without opening an image. Hand a new agent the sheet and a tile id and it
can reproduce exactly what you were looking at:

```sh
./sheet.sh S2-T07 && open tiles/S2-T07.png
```

## What a tile is

Each model renders twice: once at **stock defaults**, and once with `$fn` forced to
the fragment count the model actually achieves at those defaults, from
`ceil(max(min(360/$fa, 2*pi*r/$fs), 5))`. Those two tiles side by side **are A2
check 1 made visible** — the same solid, arriving two ways. If they differ, the
classifier is reading render variables, which is what promise 2 forbids.

Models carrying no curvature render once; `expect.txt` marks them `-`.

`mesh.py` reads the OFF and answers A1 and A3:

| | |
|---|---|
| `bnd` | edges on exactly one face — an open surface. **Identically zero on anything this pipeline exports; see below.** |
| `nonman` | edges on more than two faces |
| `nmvert` | **pinched vertices**: a vertex whose incident faces form two or more edge-connected fans. The surface touches itself at a point |
| `comp` | connected components, against the number the model's source builds (`--comp`, default 1) |
| `chi` | Euler characteristic. **Odd is a proof of invalidity**, not a measurement — no closed orientable surface has one |
| `genus` | reported only when the mesh is closed, orientable **and unpinched** |
| `throat` | beside a nonzero genus: how narrow the handle is, by proxy |

### The criterion was wrong until 2026-08-05, and it was wrong by omission

It checked non-manifold *edges* and never non-manifold *vertices*, and printed a
`genus` regardless. A surface pinched at a point has **no** offending edge — every
edge at the pinch is still carried by exactly two faces — so it announced itself
only through χ parity, which is a coin flip on the pinch count. `tee` at r=0.5 has
**two** point-attached slivers and no tunnel anywhere, and read `VALID χ=4 genus=1
comp=3`. Four changes, all in the criterion and none of them the tolerance:

1. **`nmvert`**, its own count, located on the output line. A vertex's incident
   faces are joined when an edge *at that vertex* carries both; more than one
   group is more than one fan. A closed surface's fan is a cycle rather than an
   open strip and the check does not care — it counts fans, not their shape.
2. **`genus` is suppressed when `nmvert` is nonzero**, as it already was for
   `bnd`/`nonman`. A pinched surface is not a closed orientable one and its genus
   is a number with no referent.
3. **`comp` is compared against what the model builds.** A single-union model
   returning two components has shed a fragment — `tee_small` sheds a fully
   detached 6-triangle 0.35 × 0.10 × 0.40 mm piece that shares *no* vertex with
   the body, and `cross` at r=0.9 a 4-triangle shard of 3.45e-7 mm³ floating
   outside the solid. The expectation is **declared by the model**, in a
   `// mesh.py-comp: N` line that `sweep.sh` reads; `shallow_crease` declares 2
   because it renders two plates on purpose, and its nine `comp=2` rows stay
   green. Nothing is hardcoded in the reader.
4. **`throat` beside a nonzero genus**, so a 1.5 µm handle is not weighed the same
   as a 0.4 mm loose sliver. Implemented as a **proxy, and it says so**: the first
   weld tolerance on a decade ladder at which the genus stops being what it was,
   capped at the median edge length so the weld is closing throats and not
   facets. `throat<=0.01mm` reads *these handles do not survive welding at
   0.01 mm*; a hole the model is meant to have survives every weld tried and
   reports nothing at all. `cross` r=1.5 and r=2.0 read `throat<=0.01mm`, against
   the 1.5 µm ball-removal throat and a 0.2 mm layer. `hole_plate`'s genus 1 —
   same genus, same every other number — reports no throat at all, at all twenty
   of its cells. The exact instrument is the shortest non-contractible cycle and
   it is not worth its cost here.

   **Two narrower-looking definitions were tried first and both were wrong**, in
   opposite directions, and the pair is why the selftest asserts `cross` and
   `hole_plate` together rather than either alone. The closest pair of vertices
   sharing no face read 0.0015 mm on `hole_plate` — it was measuring facet
   spacing across a fillet seam. Adding a six-hop separation test to that read
   0.84 mm on `cross`, because a handle narrower than a facet has its two sides
   one hop apart, so the test blinded the instrument to exactly what it was added
   for. Welding is the one test that scales with the handle rather than with the
   tessellation.

A mesh is **VALID** when `bnd == 0`, `nonman == 0`, `nmvert == 0`, χ is even, and
`comp` is what the model declares. `./mesh.py --selftest` runs the synthetic
controls — a cube, a torus, two disjoint cubes under both declarations, two cubes
meeting at exactly one vertex, two cubes sharing exactly one edge, three cubes
chained corner to corner (which reproduces `tee` r=0.5's exact signature, χ=4
comp=3 and no non-manifold edge, from arithmetic alone), and a pair of tori whose
holes are 4 mm and 0.0002 mm and which differ in no other number. `sweep.sh
--selftest` runs it before it renders anything.

### `bnd` cannot fire here, and this README used to say it could

The claim that a truncated open bead shows up in `bnd` and nowhere else is
**false**, and it is false by construction rather than by accident:

- `fillet()` builds its tool as a `manifold::Manifold` and hands the caller a
  Manifold boolean of it against the target. Every edge of that mesh is carried
  by exactly two faces.
- `mesh.py` welds vertices, which can only *merge* edges, so every count it ends
  up with is a sum of twos.
- The one face it drops is one welding collapsed, and such a face contributes an
  even count — two — to the single edge it has left.

So no edge can come out carried by exactly one face. `bnd` is zero on a
correct solid and zero on a broken one, and 37 of 37 tiles read zero. The
non-manifold edges that do appear are carried by **four** faces, never three,
which is the same parity argument seen from the other side.

What a truncated bead actually costs is a *visible* blunt end on a solid that is
still closed — so A1's numbers cannot see it and A5's contact sheet is what it
is for. The one topological fault found at a refusal site to date is a sliver
edge carried by four faces, and it shows up in `nonman`, not `bnd`.

**Weld tolerance is stated on every line and is not a detail** — the same mesh has
read 205 non-manifold edges at 1e-5 and 0 at 1e-6 in this effort. Default 1e-6,
`--tol` to change it, and never quote a count without it.

## Sheet 5 — the corner-dispatch page

```sh
./corner-sheet.sh           # the whole page, and re-bundles sheets/sheets.pdf
./corner-sheet.sh S5-T02    # one tile, back into its sheet
```

Sheets 1–4 are whole models at `--viewall`: they answer *is the mesh valid*, and
at that zoom a 2 mm blend on a 40 mm bracket is four pixels. Sheet 5 answers the
other question — **what does the corner actually look like** — with twelve tiles,
each the same stock-defaults render as its whole-model tile, with the camera
parked on one junction vertex read off the model's own source. No `--viewall`,
no `--autocenter`: the aim is the point.

`fillet()` blends a mixed-sign corner through a dispatch chain — an exact torus
patch when the gate proves the strips and the corner are one torus (square
elbow, equal radii), otherwise the roll-field loft. The page puts one tile on
each class, so a change that moves a corner from one branch to the other is
visible as a change in *shape*, side by side with the classes that did not move:

| tiles | class |
|---|---|
| S5-T01…T07 | `torus` — lbracket both elbows, box_step both elbows, rib, pocket, shallow_crease |
| S5-T08, T09 | `loft:T`, `loft:star` — cylinder junctions |
| S5-T10 | `loft:oblique` — the same tee off-axis |
| S5-T11 | `loft:partial` — a built bead running into a refused crease |
| S5-T12 | `loft:curved` — 48-facet trunk, 10-facet branch |

**The class in the caption is documented, not measured.** `fillet()` echoes one
line per call — edges blended, verts, tris — and says nothing about which corner
path fired, and nothing here instruments it. The labels record what each corner
is *expected* to take, which is exactly why a tile that stops looking like its
label is a finding rather than a repaint.

`sheets/INDEX.md` carries the page in its own table: tile, model, corner,
dispatch and the literal `--camera` string, so a tile id resolves to model +
corner + camera without opening an image. A full `./sheet.sh` runs this page
last, before the PDF; run standalone it re-bundles `sheets/sheets.pdf` itself.

## Sheet 6 — the torus-gate boundary page

```sh
./corner-sheet6.sh          # the whole page, and re-bundles sheets/sheets.pdf
./corner-sheet6.sh S6-T05   # one tile, back into its sheet
```

Sheet 5 puts one tile on each dispatch class using models the corpus already
had. Sheet 6 does the opposite: **twelve models that exist only to sit just
outside, or just inside, the exact-torus corner gate** — the gate that fires at
a canonical mixed corner (crease perpendicular to the terminating face, 90°
dihedrals, planar walls) and hands everything else to the loft chain. Nothing in
sheets 1–5 covers an oblique end face, a non-square trough, a curved wall, a
crowded arm or the inverse elbow, so nothing in them could show the gate's edge
moving.

| tiles | what they probe |
|---|---|
| S6-T01, T02 | end face raked 15° and 40°, crease meeting it at 75° and 50° |
| S6-T03, T04 | 60° and 120° troughs, square end face (`elbow_dihedral120` is a leaning wall, so it carries one of each) |
| S6-T05, T06 | curved end wall, curved trough wall |
| S6-T07 | end face kinking 10° **exactly at the crease** — the surface-grouping threshold |
| S6-T08, T09 | arms 6 thick against 2R = 5.8, then 3 thick against 2R = 4 |
| S6-T10 | `step_notch`, the inverse elbow: two concave and one convex at one vertex |
| S6-T11, T12 | controls that must still take the torus path — the same square elbow rotated 30° about z at a third the scale, and at 3× the scale with 3× the radius |

**These twelve are not in `expect.txt`, on purpose.** Sheets 1–4 and the 353-cell
sweep are a measured baseline; adding twelve unmeasured models to the corpus
would move every headline count in this file for no gain. They are staged here
instead, and `corner-sheet6.sh` is self-contained — it renders and montages only
sheet 6, then re-bundles the PDF.

**The dispatch column is documented, not measured**, exactly as on sheet 5.
Nothing instruments which corner path fired; the labels record what each corner
is *expected* to take. **The mesh verdict is measured**: every tile exports an
exact ASCII STL from the same invocation set as its picture and runs `mesh.py` on
it at weld 1e-6, and the verdict is on the label. These are new models in new
territory and a broken one is the point of the page, so the tile says so itself
rather than leaving it to a footnote.

Two of the twelve were *rebuilt* after their first render, and the reason is worth
keeping: a "curved" wall at stock defaults is `$fa`-bound to 12° facets, so a
large-radius cylinder puts the whole corner inside a single facet and quietly
becomes a plain oblique elbow. `elbow_curved_endface` and `elbow_facet_endface`
first rendered to *identical* mesh counts, which is what gave it away. The
curved-face model now uses a 9.5 mm cylinder so the crease terminus is three
facets wide, and the facet model kinks by 10° exactly at the crease.

`sheets/INDEX.md` carries the page in a `## Gate boundary (sheet 6)` table: tile,
model, what it probes, expected dispatch, the measured mesh line and the literal
`--camera` string. One caution on that file — `corner-sheet.sh` truncates the
index from its own `## Corner dispatch` heading to the end, so running it *after*
this page drops the sheet-6 table; re-run `./corner-sheet6.sh` to put it back.

## First run, 2026-08-04 — superseded, and its table is gone

33 tiles against the tree at `39557c022`, five of them bad at stock defaults, in
about two minutes. **That table has been removed rather than updated, because
the tree it describes no longer exists.** It was taken while the crease
threshold was still derived from the caller's facet angle; the threshold is now
the constant 46°, and one row of that table — `cross` producing no mesh at all,
having selected 374 convex edges of 757 *at an 18° threshold* — names a number
that cannot occur on this tree. Nothing in it can be quoted as current.

What replaced it is the two-axis sweep, 353 cells rather than 33 tiles, in
`results/sweep-fd3dec78.tsv`: **21 cells not valid**, enumerated in *What the
corrected criterion found* below. That is the current state of the tree and the
only list to read from.

Two findings of that first run outlived their table and are kept here as what
the run established at the time:

- **It reclassified D22.** D22 was recorded as a cosmetic fault — burrs on rims
  at stock defaults. It is a *validity* fault: models that are invalid solids at
  one tessellation are valid at another. D22 breaks promise 1, not only promise
  2. The sweep's `$fn`=8 family is the same finding on a wider axis.
- **The bench confirmed its own prediction**, which is what made it trustworthy:
  `tee_large` (d=40, above the ~19 mm diameter where `$fa` takes over from
  `$fs`) was valid both ways, exactly as the binding-term analysis said it would
  be. A model predicted safe measured safe, and the models predicted unsafe
  measured unsafe.

## The two documentation models

Neither is a gate check; both are rendered because the manual points at them.

- **`shallow_crease`** is the reason `min_angle` exists. A chevron plate whose
  fold turns 30°, below the 46° constant, so at stock defaults the fold stays
  sharp while every 90° crease on the same plate is blended. The tile renders two
  solids: the left at defaults, the right with `min_angle = 25`, which selects
  the fold — measured, 0 concave feature edges against 1, and 16 convex against
  17. That is the case the manual claims and the one a reader will hit.
- **`mixed_fn`** used to hold that job and no longer can: a union of two
  cylinders at `$fn` 48 and 10 is **valid at stock defaults with no
  `min_angle`**, because a constant threshold does not care how many facet scales
  a mesh carries. It is kept as a control for promise 2 instead. It is the model
  a mesh-derived threshold would fail on, having two dense bands to choose
  between and no reason to prefer either, so a clean render here is evidence that
  nothing is reading the mesh.

## The two axes — `sweep.sh`

`sheet.sh` renders one point per model. `sweep.sh` walks two axes around it,
numbers only, no PNG and no montage — which is what makes it cheap enough to run
over a matrix.

```sh
./sweep.sh --selftest             known answers only, about two minutes
./sweep.sh                        the whole cross, resuming
./sweep.sh --models rib --repeat 5
./sweep.sh --fn 14,26,32 --r 0.2,0.9
```

Every model now carries a top-level `R = <its own radius>;` alongside `FNSET`,
so `-D R=0.9` moves the size the same way `-D FNSET=14` moves the tessellation.
The defaults are the values the models already used: the single-point bench
results do not move, and that was checked rather than assumed — all 24 models
were rendered before and after the edit and every mesh statistic is identical.

The sweep is a **cross, not a grid**: the `$fn` axis is walked at the model's own
radius and the radius axis at stock defaults, because the full product is 24 × 11
× 9 renders and most of it is redundant. `--grid` asks for the product anyway on
a filtered model. Planar models get no `$fn` axis at all — `expect.txt` marks
them `-`, and sweeping `$fn` over a planar model tests nothing (one whole
reported series in this effort was vacuous that way).

Every row is appended to `results/sweep.tsv` as it completes and a re-run skips
rows already there, so a run killed by the stall watchdog leaves everything it
finished. Every row states its weld tolerance, its warning count, how many times
the cell was rendered, and **the mtime of the binary that produced it** — a
results file that mixes two builds says so instead of reading as one table. That
column is not hypothetical: the binary was replaced under this instrument once
already.

### It is rendered more than once per cell — once because it had to be, now because it is the check

`rib_into_boss` at `$fn`=14 returned a **valid** mesh on 2 of 40 identical
invocations of one unchanging binary, and an invalid one on the other 38. The
good runs were not vertex-order noise, which the record already knew about — they
were a different mesh, `v=226 e=672 f=448` against the usual `v=226 e=672 f=450`,
two faces fewer and no non-manifold edge. A second flavour of the same fault
appeared under an earlier binary as `v=228 f=452`. A single render per cell would
therefore report a real fault as clean roughly once every thirty cells.

**That is fixed.** The cause was an out-of-bounds read in `chainBulges()`: a
section overrunning a seam vertex takes a negative chain parameter, and
`static_cast<int>(floor(q)) % nsta` stays negative, so the builder read the 24
bytes *before* a station buffer and used whatever the allocator had left there.
Every measurement on this branch taken on a model with a seam vertex was reading
that. Measured after the fix on binary `md5 11b6b3b1`, exact ASCII STL, weld
1e-6: **60 identical meshes in 60 runs at `$fn`=14, 40 in 40 at `R`=1.0** (no
SIGBUS, all rc=0), and one mesh in 8 runs at each of `$fn` 8, 11, 14, 19, 25, 26,
32 and 48 — 164 renders, one mesh per cell, in `results/determinism-11b6b3b1.txt`
and `results/rib-fn-axis-11b6b3b1.tsv`.

The repeat is **kept anyway**, and `--selftest` now asserts the opposite of what
it used to: `distinct > 1` on either control model means the read is back. So
`sweep.sh` still renders each cell `--repeat` times (default 3), aggregates to
the **worst** outcome — a solid that is invalid on any run is not a valid solid —
and records `distinct`, the number of different meshes that came back.
`distinct > 1` is printed as `RUNS DISAGREE` and is a finding, not noise.

### What it must reproduce before it is believed

`--selftest` runs 23 checks and refuses the sweep if any fails. It asserts the
known answers, the plumbing, two invariants and the exporter comparison:

- `rib_into_boss` invalid at `$fn` 11, 25 and 32; valid at 26 and 48.
- `refused_neighbour` invalid at r 0.2, 0.8, 0.9, 0.95, 1.0 and 1.05; valid at
  0.3, 0.5 and 1.2.
- **The determinism pair.** `rib_into_boss` at `$fn`=32 must return exactly one
  mesh across 16 runs and `refused_neighbour` at r=0.9 exactly one across three.
  Until 2026-08-05 this check demanded the *opposite* of the first model, and
  correctly: it flaked. It no longer does, and a second mesh appearing here now
  means the seam-vertex read is back — in which case nothing below is readable.
- **The OFF/STL comparison must fire** on `refused_neighbour` at r=0.8, where
  the OFF is known to lose a real 5.7e-7 mm distinction.
- `-D FNSET` and `-D R` must actually reach the model — otherwise every check
  above passes on whatever the default happens to give, and the sweep is one
  number reported eleven times.
- **`selfproof.scad`**, the self-proving case: a tee rotated about z by exactly
  one facet is the same solid moved rigidly, so every count must be identical at
  `ROT=0` and `ROT=1`. It is a tee rather than a lone cylinder because a
  cylinder about z maps onto itself under a facet rotation and would pass
  vacuously.

These are **not** the expectations this file shipped with. They were re-derived
on 2026-08-05 against a pinned binary and exact STL, and several moved. The old
ones were not wrong when they were taken — the code moved under them and they
were kept past their evidence. That is the failure mode this effort keeps
hitting, so: do not restore a number here because it is written down somewhere.

### The reader is exact ASCII STL, and here is why that was checked

`src/io/export_off.cc:58` streams vertices with default `ostream` precision —
**six significant figures**. `src/io/export_stl.cc` prints through
`double_conversion::ToShortest`, which round-trips a double exactly. So the
question was raised whether the bench has been measuring the exporter rather
than the operator, and whether part of every failing set on record is an
artefact.

**The mechanism is real, and it was proven from the file rather than argued.**
In `refused_neighbour` at r=0.8, two vertices 5.7e-7 mm apart at a coordinate of
1.2 print as the same line, `1.2265 -4 7.63854`, and are distinct in the STL:

```
1.2265011588280614  -4  7.6385372358571768
1.2265017206799     -4  7.6385373349268155
```

**Its consequence for this bench, measured over the whole sweep, is nil.** Of
353 cells, 10 lose a vertex in the OFF and **0 change verdict**. The `agree`
column carries this on every row, recomputed every run, so the day it does
change a verdict the sweep will say so.

Two corrections came out of checking it, and both were instrument faults on this
side rather than exporter faults:

- The first comparison flagged 30 cells, most of them cubes. OFF writes the
  polygons it has and STL writes triangles, so a cube reads `f=6 e=12` one way
  and `f=12 e=18` the other, both χ=2 and both valid. `--compare` now tests the
  verdict invariants — valid, comp, bnd, nonman, chi, genus — and reports a
  vertex-count difference as a note rather than as a disagreement.
- The claimed false reds are **not false**. Read from exact STL and welded at
  1e-6, `rib_into_boss` at `$fn` 25 and 32 and `refused_neighbour` at r 0.9,
  0.95, 1.0 and 1.05 are all invalid, and each stays invalid **across seven
  decades of weld tolerance, 1e-4 through 1e-12**. They read valid only at
  1e-15, which is effectively no welding at all — and an unwelded reading cannot
  see a self-touch, because Manifold's output is 2-manifold by index
  construction. That regime is the `bnd` mistake over again: clean on a correct
  solid and clean on a broken one alike. The disagreement was never OFF versus
  STL; it was welded versus unwelded.

Control, for the same table: `refused_neighbour` at r=0.5 is valid at every
tolerance from 1e-3 to 1e-15. The instrument is not simply calling everything
invalid.

### What the two axes found

353 cells, one pinned binary (`md5 11b6b3b1`, built 2026-08-05 01:08 at
`cab639ffd`, with both the `dropVolumelessParts` cap and the `chainBulges`
seam-vertex fix), exact ASCII STL, weld 1e-6, 3 renders per cell aggregated to
the worst outcome. **16 cells are not valid**, and **no cell disagreed with
itself** — `distinct` is 1 on all 353, against 14 cells at 2 before the fix.

> **This table is the criterion that had no pinched-vertex check.** It is kept
> because the twelve edge faults in it are unchanged and because the diff against
> it is how the five missed cells were counted. The true failure list is 21 cells
> and it is the section below.

| model | fn | r | outcome | nonman | chi | warn |
|---|---|---|---|---|---|---|
| `boss_plate` | 8 | own | INVALID | 8 | 6 | 0 |
| `hole_plate` | 8 | own | INVALID | 8 | 4 | 0 |
| `two_bosses` | 8 | own | INVALID | 6 | 5 | 1 |
| `dome` | 8 | own | INVALID | 4 | 4 | 0 |
| `pipe_into_face` | 8 | own | INVALID | 2 | 3 | 1 |
| `rib_into_boss` | 14 | own | INVALID | 3 | 4 | 0 |
| `rib_into_boss` | 32 | own | INVALID | 4 | 4 | 0 |
| `tee` | def | 0.9 | INVALID χ-odd | 0 | 3 | 0 |
| `tee_oblique` | def | 0.2 | INVALID χ-odd | 0 | 3 | 1 |
| `tee_oblique` | def | 0.3 | INVALID χ-odd | 0 | 3 | 1 |
| `tee_oblique` | def | 0.8 | INVALID χ-odd | 0 | 3 | 1 |
| `cross` | def | 0.3 | INVALID χ-odd | 1 | 3 | 1 |
| `refused_neighbour` | def | 0.2 | INVALID | 1 | 2 | 2 |
| `refused_neighbour` | def | 0.8 | INVALID | 1 | 2 | 2 |
| `refused_neighbour` | def | 0.9 | INVALID | 1 | 2 | 2 |
| `refused_neighbour` | def | 1.0 | INVALID | 1 | 2 | 2 |

### What the corrected criterion found — 21 cells, 2026-08-05

The table above is what the **old** criterion saw. The same 353 cells, re-swept on
one pinned binary (`md5 fd3dec78`, the same source tree as `11b6b3b1`), exact
ASCII STL, weld 1e-6, 3 renders per cell aggregated to the worst outcome:
`results/sweep-fd3dec78.tsv`, diffed in
`results/sweep-compare-11b6b3b1-vs-fd3dec78.txt`.

**21 cells are not valid, not 16.** No cell went the other way — nothing the old
criterion called invalid is valid under the new one — and `distinct` is 1 on all
353 again.

**Five cells newly fail**, all of them debris the old criterion had no check for:

| model | fn | r | why it now fails |
|---|---|---|---|
| `tee` | def | 0.5 | **2 pinched vertices**, comp 3. Two point-attached slivers, 4 and 8 triangles, at (1.913417162, ∓4.619397663, 8.086582838). It read `VALID χ=4 genus=1 comp=3`: two pinches make an even χ, and there is no tunnel anywhere for that genus to describe |
| `tee_small` | def | def | a **fully detached** 6-triangle fragment, 0.35 × 0.10 × 0.40 mm, sharing zero vertices with the body |
| `tee_small` | 10 | def | the same fragment, same size |
| `tee_small` | def | 1.5 | the same fault, 0.79 × 0.25 × 0.79 mm |
| `cross` | def | 0.9 | a detached 4-triangle shard, 0.044 × 0.108 × 0.057 mm at (4.5797, −5.0608, −3.8475), outside the solid |

**Four cells keep their verdict and change their reason** — the χ-odd family,
`tee` at r=0.9 and `tee_oblique` at r 0.2/0.3/0.8. Each is now reported as one
pinched vertex *with its coordinates*, rather than as an odd Euler characteristic;
parity was the symptom and the pinch is the fault. For `tee` the pinch is at
(1.913417162, −4.619397663, 8.086582838), the same vertex at every radius.

**The other twelve are unchanged in verdict and in reason**: the `$fn`=8 family
(`boss_plate`, `hole_plate`, `two_bosses`, `dome`, `pipe_into_face`), `cross` at
r=0.3, `refused_neighbour` at r 0.2/0.8/0.9/1.0, and `rib_into_boss` at `$fn` 14
and 32. All are non-manifold *edges*, which the old criterion did see.

Two things that did **not** move, and both were checked rather than assumed:

- **`shallow_crease`'s nine `comp=2` rows stay green.** The model declares
  `// mesh.py-comp: 2` in its own source, because it renders two plates on
  purpose. Nothing else in the bench declares anything.
- **`cross` at r=1.5 and r=2.0 stay valid**, genus 2 and 4, now carrying
  `throat<=0.01mm` — the handles do not survive a 0.01 mm weld, i.e. they are
  micron-scale boolean noise 20× under a print layer. `hole_plate`'s genus 1, at
  every one of its twenty cells, survives every weld the tessellation allows: it
  is the hole the plate is named for. Same genus, same every other number,
  opposite meanings, and until this column existed nothing in the table
  distinguished them.

### Which of these were the memory bug: one of them

The same 353 cells were swept before the seam-vertex fix (`results/sweep-21cd49a8.tsv`)
and after (`results/sweep-11b6b3b1.tsv`); the full diff is in
`results/sweep-compare-21cd49a8-vs-11b6b3b1.txt`. **12 cells moved, and all 12
are `rib_into_boss`** — the only bench model with a seam vertex, which is the
only place the out-of-bounds read could fire.

- **One verdict changed**: `rib_into_boss` at `$fn`=12 was *no output* and is now
  VALID, `v=173 e=513 f=342`, χ=2, genus 0 — 20 identical runs, 0 warnings.
- **Eleven cells kept their verdict and moved their mesh**, all `rib_into_boss`,
  typically a handful of vertices (`v=226 e=672 f=448` → `v=222 e=660 f=440` at
  the default radius). The pre-fix numbers were computed partly from whatever
  the allocator had left in the 24 bytes before a station buffer.
- **341 cells are identical in verdict *and* in every mesh count.** The read was
  reachable only through a seam vertex, so the rest of the table was never
  affected, and the pre-fix sweep's readings on those 341 stand.

So of the seventeen cells the earlier sweep recorded as not valid, **exactly one
was the memory bug**. The `$fn`=8 family, the four χ-odd cells, `cross` at r=0.3,
`refused_neighbour` at four radii and `rib_into_boss` at `$fn` 14 and 32 all
reproduce on the fixed binary and are real geometric defects. `rib_into_boss` at
`$fn`=32 is *worse* than recorded once the garbage is gone: nonman 4 and χ=4,
against nonman 2 and χ=3.

Three things in the table are new.

**`$fn`=8 breaks five models that are clean at every other tessellation** —
`boss_plate`, `hole_plate`, `two_bosses`, `dome`, `pipe_into_face`. All five are
valid at 10 and above and all five are boss-on-plate or hole-in-plate shapes.
This is a single coarse-tessellation family, and no single-point bench could
have seen it.

**Four cells are invalid with `nonman=0` and an odd χ.** `tee` at r=0.9 and
`tee_oblique` at r 0.2, 0.3, 0.8 have no non-manifold edge at all — the proof of
invalidity is the odd Euler characteristic alone. Three of the four carry a
warning, so a reader who trusted the warning count would have caught them; `tee`
at r=0.9 carries **no warning**, and is the case for keeping both instruments.

**`cross` gains genus as its radius grows**: genus 0 at r ≤ 1.0, genus 2 at
r=1.5, genus 4 at r=2.0, all reported VALID because a closed orientable surface
of genus 4 is a valid solid. Whether a fillet is allowed to punch two extra
handles through the model is a question for the operator's promise, not for A1,
but nothing before this sweep would have shown it.

### `rib_into_boss` no longer disagrees with itself

It used to, at every `$fn` tested: two distinct meshes in eight identical runs
at 8, 11, 14, 19, 25, 26, 32 and 48, with `$fn`=14 flipping validity 6/2. That
table is the pre-fix one and is kept in the git history; on binary `md5
11b6b3b1`, at weld 1e-6, over eight identical runs per `$fn`
(`results/rib-fn-axis-11b6b3b1.tsv`):

| `$fn` | outcome | distinct meshes in 8 runs |
|---|---|---|
| 8 | valid 8/8 | 1 |
| 11 | **invalid 8/8** | 1 |
| 14 | **invalid 8/8** | 1 |
| 19 | valid 8/8 | 1 |
| 25 | **invalid 8/8** | 1 |
| 26 | valid 8/8 | 1 |
| 32 | **invalid 8/8** | 1 |
| 48 | valid 8/8 | 1 |

`$fn`=14 is invalid, not flaky: 8/8 here and 60/60 in a dedicated run
(`v=222 e=660 f=442`, nonman 3, χ=4, identical at weld 1e-4, 1e-6 and 1e-9, no
warnings). `R`=1.0, which used to exit SIGBUS about one run in six, is 40/40
identical, VALID, rc=0 throughout. `refused_neighbour` remains the deterministic
control it always was, so the pair still separates a real difference from a
reader inventing one — it now does it by agreeing rather than by disagreeing.

## Known gaps, stated rather than left silent
- **The sweep is one binary deep.** All 353 rows of `results/sweep.tsv` are
  `md5 11b6b3b1`; the pre-fix table in `results/sweep-21cd49a8.tsv` is all
  `21cd49a8`. The binary was replaced under this instrument four times in one
  session, so each table was taken against a copy pinned aside; neither says
  anything about any other build, and the `bin` column is there so that stays
  honest. `results/sweep.tsv` is deliberately the *current* table, because a
  default re-run resumes into that file and would otherwise silently skip every
  cell and report the old binary's answers as if they were this one's.
- **The `cross` no-output case and the `$fn`=8 family were not re-derived
  cell-by-cell**, only re-swept: they reproduce identically on the fixed binary,
  which is what rules the memory bug out as their cause, but nothing here
  explains them.
- **`$fn` 11 and 25 are in the selftest but not the sweep axis.** They were
  found while re-deriving known answers and the default axis was left alone.
- **No STL round trip yet** — A2 check 2. It needs a wrapper that exports a model
  and re-imports it, and it is the check that proves the classifier reads the mesh
  rather than the file.
- **No non-Manifold build check** — that the modules are absent rather than silent.
- The `cross` no-output case is recorded, not diagnosed.
