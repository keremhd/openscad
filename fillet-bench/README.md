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
| `bnd` | edges on exactly one face — an open surface. **A bead truncated and left open (D24) shows up here and nowhere else.** |
| `nonman` | edges on more than two faces |
| `chi` | Euler characteristic. **Odd is a proof of invalidity**, not a measurement — no closed orientable surface has one |
| `genus` | reported only when the mesh is closed and orientable |

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

## Known gaps, stated rather than left silent

- **No radius sweep.** Each model carries one size. Sweeping it needs `R`
  parameterised at the top of each model and a second axis in `expect.txt`; the
  size gate's behaviour across radii is exactly what that would show.
- **No STL round trip yet** — A2 check 2. It needs a wrapper that exports a model
  and re-imports it, and it is the check that proves the classifier reads the mesh
  rather than the file.
- **No non-Manifold build check** — that the modules are absent rather than silent.
- `FLAGS` in `sheet.sh` is where `--enable=fillet` goes once the feature is gated.
- The `cross` no-output case is recorded, not diagnosed.
