# Fillet operator — case suite

Hand-written reference geometry for the fillet/round/chamfer/bevel tools, and
two ways to use it: a **picture** you read, and a **verdict** you can run in a
loop. Both are driven by the same case files, so a case is written once and gets
both.

This lives outside CTest. The cases that are green have been promoted into
`tests/data/scad/3D/features/` with committed baseline PNGs — the chamfer and
bevel ones so far — and the containment comparison below also exists as a Catch2
test (`src/geometry/fillet/FilletCompare_test.cc`) that runs under `ctest`. This
directory stays as the place where a new case is prototyped before it has a
baseline, and as the only place that draws the picture: a promoted case tells you
*whether* it is right, the six columns here tell you *what* is wrong.

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

[`junction-cases.md`](junction-cases.md) is where the corner configurations the
operator had never been pointed at were written down before they were built —
vertices where creases of both signs meet, junctions whose corner solve is
refused, a bead that rolls into a face it shares no crease with — and what each
one turned out to do. Several were expected to come out wrong and two do. The
point was to find out and write the outcome down, not to wait until they pass.

[`next-cases.md`](next-cases.md) is the queue that replaced it: curved creases
and the junctions on them, and the brush configurations past the half-chain case.
Each entry says whether it was probed by hand or only reasoned about, so whoever
writes them knows which ones already have an answer to pin and which are still
questions. It also records what was queued and then **withdrawn**, and why —
because a case here costs a CGAL dilation on every run, and most of what that
file first asked for turned out to assert a count, which
`src/geometry/fillet/FilletBuilder_test.cc` pins exactly and for free. When in
doubt about a new case, that is the question to ask first: is the answer a shape,
or is it a number?

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

That equivalence is why columns 2, 4 and 6 are drawn **only for a `ref` variant**
(see below). A variant with no hand-written answer runs no `tool` check, so a red
column there would be a disagreement with a shape that was never the answer, and
the green columns would be claiming a known-good the case doesn't have. They come
out blank instead, which also spares the row its one expensive computation.

Every case is sliced to a thin slab so one flat image is legible — a horizontal
cut, a vertical cut through a ring axis, or a stack of horizontal cuts at rising
z for the junction cases, where the point is to watch the section collapse toward
the meeting point.

Sections are what a flat image can carry, not what you always want to look at, so
opening a case file in the GUI repeats the whole thing **as solid rows**: every
variant, all six columns, with no slab at all, laid out past the sliced rows
along the case's own `CASE_DY`/`CASE_DZ` stepping. A section tells you where two
shapes differ; the solids are the only place you see what the difference looks
like on the part, and they can be turned around. `render.sh` passes
`-D FILLET_NO_SOLID=true`, so the PNGs stay the six columns above — in a fixed
top-down image the solids would only hide the sections behind their outlines.

## The verdict

```
./check.sh case_boss_base_fillet                 # every variant x every check
./check.sh case_boss_base_fillet large tool      # one check
./run_all.sh                                     # everything, vs expectations
```

Each check reduces to "is this solid empty?" — the one verdict that needs no
committed baseline:

| check | passes when | catches |
|---|---|---|
| `tool` | the residual against the hand reference is empty | wrong radius, missing corner, gouge, wrong sign |
| `sandwich` | the applied result and the model stay within `size` of each other | gouges, runaway or wrong-direction tools — **needs no reference** |
| `emits` | the candidate tool is non-empty | the operator silently doing nothing |
| `drops` | the candidate is empty **and** the log carries a warning | a size the feature cannot carry being built anyway, or refused in silence |

All of them are evaluated inside `case_clip()`, the region the case is about.
`tool` is the sharp one. `sandwich` is deliberately coarse — `selftest_wrongradius`
passes it — but it is what gives the junction corners automated coverage at all,
since the equal-radius trihedral and valence-4 corners have no closed form to
compare against.

`drops` is the only check that reads the log for more than errors, and it has to:
an empty result is exactly what a deliberate refusal and an unimplemented
operator have in common, so emptiness alone would let the second pass as the
first. The warning is half the contract.

### Which checks a variant gets

A variant declares its `kind`, and that decides:

| kind | means | checks |
|---|---|---|
| `ref` | a hand-written answer exists | `tool` `sandwich` `emits` |
| `drop` | the size is out of range for the feature — the required output is a warning and no tool | `drops` `sandwich` |
| `none` | no closed form exists to compare against | `sandwich` `emits` |

The two reference-free kinds are worth keeping apart. `none` is the junction
corners, where the equal-radius blend has no elementary solution and nothing can
be written down. `drop` is a *decision*: an oversized radius has a perfectly
well-defined answer — refuse it and say so — and `drops` checks that answer
rather than shrugging at it. Overloading one flag for both is what hid the
difference before.

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
//                name     size kind    tol
CASE_VARIANTS = [["small",   6, "ref",  0.12],
                 ["large",  35, "drop", 0.70]];

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
- **`kind`.** `ref` when a hand-written answer exists, `drop` when the size is out
  of range for the feature, `none` when no closed form exists at all. A `drop` or
  `none` variant keeps its reference-free checks rather than falling out of the
  suite, and the picture omits its three reference columns — drawing them would
  put the known-good green next to a shape nothing compares against, and would
  report a diff against an answer that isn't one.
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

`expectations.txt` is a ledger of *temporary debt* and nothing else. What the
suite considers correct lives in the case files, as each variant's `kind` — so a
permanent contract like "this size must be refused" is a `drop` variant, never an
expectation line. Written as an expectation it would read as "not there yet", and
someone would eventually delete it believing they had fixed it.

## Cost

The dilation runs through CGAL's Nef kernel (Minkowski has no Manifold path), so
checks are seconds, not milliseconds, and the diff column is the expensive part
of a render — hence `FAST=1`. CGAL also refuses geometry that is not a clean
solid; `check.sh` reports that as a failed check, because it is a statement about
the candidate rather than about the harness.
