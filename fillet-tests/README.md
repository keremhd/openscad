# Fillet operator — case suite

Hand-written reference geometry for the fillet/round/chamfer/bevel tools, and
two ways to use it: a **picture** you read, and a **verdict** you can run in a
loop. Both are driven by the same case files, so a case is written once and gets
both.

This lives outside CTest for now. Milestone M6 promotes the cases that are green
by then into `tests/data/scad/3D/features/` with committed baseline PNGs; the
directory stays afterwards as the place where a new feature is prototyped before
it has a baseline.

## Layout

```
cases/     one file per case — the whole contract, geometry and metadata
lib/       _ref.scad     hand-written reference tools (no operator involved)
           _harness.scad viewers, the six-column row, the checks
           _common.sh    binary lookup and case-metadata probing
render.sh  cases -> png/
check.sh   one case (or one variant, or one check) -> PASS/FAIL
run_all.sh everything, compared against expectations.txt
```

## The picture

```
./render.sh                            # all cases -> ./png/
./render.sh case_inner_corner_fillet    # just one
SIZE=2400,1600 ./render.sh              # bigger
FAST=1 ./render.sh                      # skip the diff column
OPENSCAD=/path/to/openscad ./render.sh  # override the binary
```

One row per size variant, six columns per row:

| column | what | colour |
|---|---|---|
| 1 | base model | grey |
| 2 | model with the **hand reference** applied | green |
| 3 | model with the **operator's** tool applied | blue |
| 4 | the reference tool alone | dark green |
| 5 | the operator's tool alone | dark blue |
| 6 | **diff** — where the two disagree by more than the tolerance | red |

Columns 4–6 are clipped to the case's region of interest; 1–3 show the whole
model. The last column is not a separate opinion about correctness: it is
literally the geometry the `tool` check evaluates, so **an empty red column and a
green test are the same fact**. Side by side tells you *what* went wrong, the
diff tells you *whether* and *where*.

Every case is sliced to a thin slab so one flat image is legible — a horizontal
cut, a vertical cut through a ring axis, or a stack of horizontal cuts at rising
z for the junction cases, where the point is to watch the section collapse toward
the meeting point.

## The verdict

```
./check.sh case_boss_base_fillet                 # every variant x every check
./check.sh case_boss_base_fillet large tool      # one check
./run_all.sh                                     # everything, vs expectations
```

Three checks per variant, each reducing to "is this solid empty?" — the one
verdict that needs no committed baseline:

| check | passes when | catches |
|---|---|---|
| `tool` | the residual against the hand reference is empty | wrong radius, missing corner, gouge, wrong sign |
| `sandwich` | the applied result and the model stay within `size` of each other | gouges, runaway or wrong-direction tools — **needs no reference** |
| `emits` | the candidate tool is non-empty | the operator silently doing nothing |

All three are evaluated inside `case_clip()`, the region the case is about.
`tool` is the sharp one and is skipped where the case declares it has no
reference. `sandwich` is deliberately coarse — `selftest_wrongradius` passes it —
but it is what gives the junction corners automated coverage at all, since the
equal-radius trihedral and valence-4 corners have no closed form to compare
against.

Comparison is tessellation-independent. An exact symmetric difference is never
empty (the operator tessellates arcs at `$fa`, the reference uses
`cylinder`/`rotate_extrude` at `$fn`), so each side is dilated by a tolerance `t`
before subtracting; empty means Hausdorff distance below `t`. Cases use 2 % of
the size, comfortably above chord error and below any defect worth catching.

`check.sh` exits 0 = PASS, 1 = FAIL, 2 = ERROR (parse error, missing binary),
3 = CRASH (the binary died on a signal — reported separately so an engine bug
can't masquerade as a geometric result).

## Writing a case

A case file is the single source of truth: geometry *and* metadata, read back by
the shell through OpenSCAD's echo export rather than duplicated in a manifest.

```openscad
include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

CASE_SIGN  = "union";        // concave tools are unioned, convex ones subtracted
CASE_SLICE = ["top", 30];    // ["top",z] | ["front",y] | ["stack",zs,spacing]
CASE_DY    = 120;            // row spacing
//               name    size  has_ref  tol
CASE_VARIANTS = [["small",  6,  true,   0.12],
                 ["large", 35,  false,  0.70]];

module case_model()     { ... }               // the solid under test
module case_ref_tool()  { ... }               // hand-written answer, or empty
module case_cand_tool() fillet_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();    // region the comparison is about

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
```

Notes on the three parts that are easy to get wrong:

- **`$case_size`.** The tool modules take no arguments; the size comes in as a
  special variable, bound per row, so one set of modules serves every variant and
  both drivers. Adding a size is one line in `CASE_VARIANTS`.
- **`has_ref`.** Set it false when no hand-written answer exists — the junction
  corners, and radii too large for the face, where clamping behaviour is not
  settled. The variant keeps running `sandwich` and `emits` rather than dropping
  out of the suite. The picture still draws whatever `case_ref_tool()` produces
  for such a variant — useful context, but the checks ignore it, so read it as
  "roughly this" rather than as the answer.
- **`case_clip()`.** Concave cases usually need none: a model can be built with
  exactly one concave edge. Convex cases always do — every solid has plenty of
  convex edges, the operator rounds all of them, and the reference covers one.
  Clip to a box or cylinder around the target edge, clear of the rims and
  corners.
- **The `FILLET_DRIVER` guard.** Opened directly, the file draws its own picture;
  the headless drivers `include` it with `FILLET_DRIVER = true` and supply their
  own top-level geometry. Top-level assignments are scope-wide and last-one-wins,
  which is what makes the override reach inside the included file.

## Expectations

Most real cases are red today, on purpose: the operator is still being built. So
`run_all.sh` compares each result against `expectations.txt` and only complains
about **surprises** — a check that changed state without anyone updating the
table. When a milestone lands, the expected-FAIL lines it fixes are what you
delete.

## Cost

The dilation runs through CGAL's Nef kernel (Minkowski has no Manifold path), so
checks are seconds, not milliseconds, and the diff column is the expensive part
of a render — hence `FAST=1`. CGAL also refuses geometry that is not a clean
solid; `check.sh` reports that as a failed check, because it is a statement about
the candidate rather than about the harness.
