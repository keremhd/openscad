# fillet-bench — the default-settings bench and contact sheets

The instrument the acceptance gate is measured with, and the one thing in this
effort designed to be *looked at* rather than reported.

```sh
./sheet.sh                  # render everything, rebuild all sheets
./sheet.sh S2-T07           # re-render one tile, drop it back into its sheet
./sheet.sh --models tee     # every tile of one model
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
| `chi` | Euler characteristic. **Odd is a proof of invalidity**, not a measurement — no closed orientable surface has one |
| `genus` | reported only when the mesh is closed and orientable |

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

## First run, 2026-08-04

33 tiles against the tree at `39557c022`. **Five bad tiles, all at stock defaults,
in about two minutes.**

| tile | finding |
|---|---|
| `S1-T01` `tee` | defaults **χ=3, 2 non-manifold edges** — invalid. At `$fn`=16, χ=2 genus 0, clean |
| `S1-T03` `tee_oblique` | defaults **χ=5, 6 non-manifold** — invalid. At `$fn`=16, clean |
| `S1-T05` `tee_small` | defaults **χ=5, 6 non-manifold** — invalid. At `$fn`=10, clean |
| `S1-T09` `cross` | defaults **produce no mesh at all**. Selects 374 convex edges of 757 at an 18° threshold, then refuses 136 of 298 |
| `S1-T10` `cross` | at `$fn`=19, **χ=3, comp=2, 1 non-manifold** — invalid |

**This reclassifies D22.** It was recorded as a cosmetic fault — burrs on rims at
stock defaults. It is a *validity* fault: the same models are invalid solids at
defaults and valid at an explicit `$fn`. D22 breaks promise 1, not only promise 2.

The bench also confirms its own prediction, which is what makes it trustworthy:
`tee_large` (d=40, above the ~19 mm diameter where `$fa` takes over from `$fs`) is
**valid both ways**, exactly as the binding-term analysis said it would be. A model
predicted safe measured safe, and the models predicted unsafe measured unsafe.

Everything else — `boss_plate`, `two_bosses`, `pipe_into_face`, `dome`,
`hole_plate`, `bevel_boss`, `rib`, `pocket`, `lbracket`, `box_step`, `thin_slab`,
`chamfer_box`, `brush_one_edge`, and both controls — is valid at defaults.

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
