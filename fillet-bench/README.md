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

### It is rendered more than once per cell, and that is a measured requirement

`rib_into_boss` at `$fn`=14 returned a **valid** mesh on 2 of 40 identical
invocations of one unchanging binary, and an invalid one on the other 38. The
good runs are not vertex-order noise, which the record already knew about — they
are a different mesh, `v=226 e=672 f=448` against the usual `v=226 e=672 f=450`,
two faces fewer and no non-manifold edge. A second flavour of the same fault
appeared under the earlier binary as `v=228 f=452`.

So a single render per cell would report a real fault as clean roughly once every
thirty cells. `sweep.sh` renders each cell `--repeat` times (default 3),
aggregates to the **worst** outcome — a solid that is invalid on any run is not a
valid solid — and records `distinct`, the number of different meshes that came
back. `distinct > 1` is printed as `RUNS DISAGREE` and is itself a finding.

### What it must reproduce before it is believed

`--selftest` asserts the known answers, the plumbing, and an invariant:

- `rib_into_boss` invalid at `$fn` 14 and 32, valid at 26.
- `refused_neighbour` invalid at r 0.2 / 0.8 / 0.9 / 1.0, valid at 0.5.
- `-D FNSET` and `-D R` actually reach the model — otherwise every check above
  would pass on whatever the default happens to give, and the sweep would be one
  number reported eleven times.
- **`selfproof.scad`**, the self-proving case: a tee rotated about z by exactly
  one facet is the same solid moved rigidly, so every count must be identical at
  `ROT=0` and `ROT=1`. It is a tee rather than a lone cylinder because a cylinder
  about z maps onto itself under a facet rotation and would pass vacuously.

### What the two axes found, 2026-08-05

Two builds are involved and they are kept apart, because the binary was replaced
by another worktree in the middle of this run.

**Build A**, `OpenSCAD` mtime 2026-08-04 19:43:44 — the build the two open
defects were recorded against. It reproduces both exactly:

| model | axis | invalid at | reading |
|---|---|---|---|
| `rib_into_boss` | `$fn` | 14 | χ=4, nonman=3, weld 1e-6 |
| `rib_into_boss` | `$fn` | 32 | χ=4, nonman=4, weld 1e-6 |
| `refused_neighbour` | r | 0.2, 0.8, 0.9, 1.0 | χ=2, nonman=1, weld 1e-6, warnings 2 |

`rib_into_boss` at `$fn`=32 reads χ=4 nonman=4 here, where STATE.md records
χ=3 nonman=2. The defect reproduces; the counts have moved, and the likeliest
reason is that D22's closure changed the model's crease selection after the
counts were taken.

**Build B**, mtime 2026-08-04 23:56:01, built from another worktree's
*uncommitted* `FilletBuilder.cc`. Recorded because it is what the committed
`results/sweep.tsv` was measured with, not as a statement about any commit:

| model | fn | r | outcome | reading |
|---|---|---|---|---|
| `rib_into_boss` | 14 | own (2) | INVALID | χ=4 nonman=3 warn=0 weld 1e-6, 5/5 runs |
| `rib_into_boss` | 32 | own (2) | INVALID | χ=4 nonman=4 warn=0 weld 1e-6, 5/5 runs |
| `rib_into_boss` | def | 1.0 | **SIGBUS** | rc=138, no output, ~1 run in 6 |
| `refused_neighbour` | def | 0.8 | INVALID | χ=2 nonman=1 warn=2 weld 1e-6, 5/5 runs |

Two things changed between the builds and both are worth someone's attention.
`refused_neighbour` is now valid at r = 0.2, 0.9 and 1.0 and still invalid at
0.8, so three quarters of that defect appears to have been fixed and a quarter
not. And `rib_into_boss` at r = 1.0 now **crashes with SIGBUS** on about one run
in six — a hard memory fault, which is also the most economical explanation of
why the same model's face count wanders between runs.

Cells whose runs disagreed with each other, all `rib_into_boss` on build B:
`$fn`=10 (3 distinct meshes in 5 runs), `$fn`=12, `$fn`=26, r=0.8, r=1.0.

## Known gaps, stated rather than left silent
- **The full sweep has not been run.** The instrument is built and validated;
  the binary under it was rebuilt by another worktree part-way through, so the
  only rows recorded are the two defect models, and they are recorded against
  one stated build. A table spanning two builds is not a table.
- **No STL round trip yet** — A2 check 2. It needs a wrapper that exports a model
  and re-imports it, and it is the check that proves the classifier reads the mesh
  rather than the file.
- **No non-Manifold build check** — that the modules are absent rather than silent.
- The `cross` no-output case is recorded, not diagnosed.
