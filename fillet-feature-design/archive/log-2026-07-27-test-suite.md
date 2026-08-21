# 2026-07-27 — merging the two test harnesses into `fillet-tests/`

Not a milestone. A structural fix to the M1/M6-prep scaffolding, prompted by the
observation that the automated set was far too small while the visual set did not
exercise the operator at all.

## The problem

M1 left two directories:

- `fillet-visual-tests/` — 7 hand-written scenes. Operator-independent by design,
  which was right *before* the operator existed and wrong after: they rendered
  the target and never asked whether the tool hit it.
- `fillet-equivalence-tests/` — 3 comparisons, of which 2 were harness
  self-tests. One real case.

The gap was not neglect, it was structural: a case had to be written twice, once
as a scene and once as a comparison, in two files that shared nothing but a
`use <>`. The automated set was always going to lag.

## The decision

One directory, one file per case, both drivers reading it. A case declares:

```
CASE_SIGN / CASE_SLICE / CASE_VARIANTS      metadata
case_model() case_ref_tool() case_cand_tool() case_clip()
```

and ends with `if (!FILLET_DRIVER) case_view();`. Opened directly it draws its own
picture; the headless driver includes it with `FILLET_DRIVER = true` and supplies
its own top-level geometry.

Three mechanisms make that work, each verified before the cases were written:

1. **`$case_size`** is a special variable bound per row by the viewer, so the
   tool modules take no arguments and one set of them serves every size variant.
   That is what answers "how do you test a small and a large radius" without
   either duplicating the case or hard-coding one size.
2. **Top-level assignment is scope-wide and last-one-wins**, so the driver's
   `FILLET_DRIVER = true` reaches the guard inside the included case.
3. **Echo export** (`-o x.echo`, ~0.25 s, no geometry) lets the shell read
   `CASE_VARIANTS` back out of the `.scad`. Metadata has one home.

## What the pictures show now

Six columns: model, reference applied, candidate applied, reference tool,
candidate tool, residual. The residual column is the geometry the `tool` check
evaluates — the picture and the verdict are the same computation, not two
opinions about it.

## What is checked now

Three checks per variant, ~30 across the suite against 3 before:

- `tool` — two-sided containment against the hand reference (the old
  `check_equivalence.sh` idea, unchanged and still tessellation-independent).
- `sandwich` — result and model within `size` of each other. **Needs no
  reference**, which is what finally gives the 3- and 4-face junction cases
  automated coverage; they had none, because the equal-radius corner has no
  closed form. Coarse on purpose: `selftest_wrongradius` passes it.
- `emits` — the candidate tool is non-empty at all.

Cases were also split one-tool-per-file, so `chamfer_tool` and `bevel_tool` get
their own cases instead of riding along in a corner of the fillet scene. All four
builtins are now covered.

Convex cases need a `case_clip()`: the operator rounds all twelve edges of a
cube while a hand reference covers one, so the comparison is clipped to a box
around the target edge, clear of the rims. Concave cases mostly need none — an L
has exactly one concave edge.

## Cost, and two things it exposed

Minkowski has no Manifold path; it falls back to CGAL's Nef kernel.

- `$fn = 128` on the ring cases was gratuitous — chord error at 48 is ~0.2 % of
  `r` against a 2 % tolerance — and at 128 the boss case did not finish in six
  minutes. Ring cases are now 48, prism cases 64.
- CGAL aborts on geometry that is not a clean solid. Today's candidate is the M3
  debug marker overlay, not a solid, so most real cases report either "CGAL could
  not dilate" or a hard `SIGABRT`. Every check is therefore bounded by
  `CHECK_TIMEOUT` (60 s), and signal deaths are reported as `CRASH`, distinct
  from `FAIL` — an engine bug must not read as a geometric verdict.

The crashes are pre-existing behaviour the suite now surfaces, not new breakage;
they are listed in `expectations.txt` so `run_all.sh` reports only *changes* in
state. When M5/M7 replace the markers with real solids, the expected-CRASH and
expected-FAIL lines they fix are what gets deleted.
