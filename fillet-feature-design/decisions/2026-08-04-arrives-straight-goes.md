# Decision — `arrivesStraight` comes out, and the curved arrival is served

2026-08-04, later the same day. **Supersedes
`2026-08-04-arrives-straight-stays.md`**, which recorded the opposite decision
and its own amendment against it.

## What was decided

`arrivesStraight` is **deleted** (`04d95aca9`). A seam vertex is served by the
seam rule whether or not some crease reaches it on a curve, and the only
question asked of a vertex is still `creaseLeavesUnfilleted`.

## Why the earlier decision was taken on a false premise

The predicate's rationale, and the case for keeping it, both rested on the
sentence *"a seated ball has no such problem, so a vertex a bead arrives at on a
curve is handed back to it."*

**Nothing is handed back to a seated ball, because there is no ball at these
vertices.** Instrumented at the two curved-arrival vertices of `bcurve.scad`,
(7.4130, ±3, 5), `chainJunctions` finds no junction at either. This is
structural rather than incidental: the crease that leaves such a vertex
unfilleted is typically one a brush cut, and the same brush withholds the corner
through `noCorner`. A vertex the seam rule is withheld from therefore has no
corner cell either, and falls to the plain stop-a-hair-short branch — whose own
comment states what that produces. The two beads reach the same point of the
sharp edge, tangent to the wall they share, meet there at no angle, and a
boolean resolves that into a knife edge.

The predicate was choosing between the overrun and nothing. It chose nothing.

## The measurement

`rib_into_boss` / `bcurve` (they are the same shape), swept over `$fn` 8 to 80
in steps of 2 — 37 tessellations, weld 1e-6, Manifold, zero "does not fit"
warnings and `Status: NoError` throughout, so nothing was refused and skipped:

| arm | invalid solids of 37 | `bcurve` at `$fn`=64 |
|---|---|---|
| predicate kept | **16** | χ=5 (odd), 5 edges on >2 faces, genus −1.5 |
| predicate deleted | **2** | χ=2, genus 0, 0 such edges |

Stable at weld 1e-7. The two that remain, at `$fn`=14 and 32, are new — they are
not in the kept arm's failing set — and are a smaller fault at the same corner,
recorded below.

## Why the regression that justified keeping it no longer exists

The three models the earlier decision stood on measure **byte-identical** in both
arms on this tree:

| model | kept | deleted |
|---|---|---|
| `box_step_fn24_r3` | genus 0 | genus 0, byte-identical |
| `box_step_fn48_r3` | genus 0 | genus 0, byte-identical |
| `box_step_fn96_r3` | genus 0 | genus 0, byte-identical |

The recorded genus −1 and −5 at `$fn` 48 and 96 do not reproduce. `box_step` is
two cubes and carries no curved crease at all, so `arrivesStraight` returns true
at every end of it and cannot be doing anything; the earlier readings were taken
through the `$fa`=12 threshold artefact that `36517b03c` fixed by reading the
threshold off the solid.

The predicate is in fact inert across the **whole** bench: every one of the 33
pre-existing tiles is byte-identical with it and without it. That is why
`rib_into_boss` was added to the bench (`f047f97ad`) before the predicate was
touched — the gate could not see the defect at all.

## The alternative that was tried and lost

Running the overrun along the **arc** through the last three spine points, by
rotating the end section rigidly about that circle's axis, instead of along the
chord its last segment lies on. This removes exactly the first-order departure
from the wall that the predicate's comment complains of.

It measures worse: **19 of the same 37 invalid**, with genuine tunnels (genus 1,
genus 2) appearing at `$fn` 58, 64 and 66. The reason is that past the seam
vertex the arc's continuation is buried inside the solid the *other* bead sits
on, so following the arc carries the overrun away from the bead it exists to
overlap. The chord's outward drift is what carries it there. The code was
written, measured and removed; it is in no commit.

Building the corner ball the straight case skips was not tried, and should not
be: an unfilleted crease leaves these vertices, which is the whole condition
under which a ball is the wrong construction.

## What is still open

- **Two tessellations of `rib_into_boss` remain invalid**, `$fn`=14 (χ=4, 3
  edges on >2 faces) and `$fn`=32 (χ=3, 2 such edges), weld 1e-6. Both are at
  the same corner, on the bead surface where the two beads cross, at |y| just
  outside the rib face. Neither is at a tessellation the bench renders, so the
  bench is green on this model; a `$fn` axis in `expect.txt` would catch them.
- **`cross` at `$fn`=19 (`S1-T10`) is still invalid** — χ=3, two components, one
  edge on >2 faces. Unrelated to this work and unchanged by it.
- The CGAL backend was not exercised. Manifold only.
