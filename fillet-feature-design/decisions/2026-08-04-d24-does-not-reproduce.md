# D24 — a bead truncated and left open at a refused neighbour — does not reproduce

Measured 2026-08-04 at `12b384714`, `--backend=manifold --render`, weld tolerance 1e-6.

## The recorded repro, run as recorded

`handoff-2026-08-03.md:876` gives an all-planar repro: a tilted box landing on a cube at
`r = 0.5`. It is now `fillet-bench/models/refused_neighbour.scad`, bench tile `S3-T12`.

It still refuses the same creases — one concave at `[7.5, 4, 7.44]` for an off-face miss of
0.052, four convex around `[7.5, -4, 7.261]` for crowding — and builds the other twenty. Both
refusals warn. The result is `VALID v=356 e=1062 f=708 comp=1 bnd=0 nonman=0 chi=2 genus=0`.

The visible symptom the owner reported *is* still there: the cube-edge bead ends in a blunt
flat face short of the tilted box. It is not a hole. Under promise 4 — the bar is a valid
solid, not a smooth corner — that is a release note.

## `bnd` could not have answered the question either way

`fillet-bench/README.md` claimed a truncated open bead shows up in `bnd` and nowhere else, and
A3 was to be measured with it. `bnd` is identically zero on any Manifold-backend export: the
result is a Manifold boolean, so every edge carries exactly two faces; `mesh.py`'s weld only
merges edges, so every count is a sum of twos; and a face the weld collapses contributes two to
the one edge it has left. 36 of 36 tiles read zero, and the non-manifold edges that do appear
are carried by four faces, never three. Recorded as broken instrument 10.

## Why the mechanism is not reachable

D24's inferred mechanism was that spine truncation happens at a junction whether or not the
neighbour was built, so a bead is shortened toward a corner cell that never arrives.

In `buildRoundSolid` truncation is applied only at vertices present in `junctionAt`, and a
refused crease keeps a vertex out of that map by either of two independent routes:

1. `chainJunctions` runs over the chain set that survived the size gate and wants two or more
   open chain ends at a vertex. A refusal that leaves one bead there leaves no junction.
2. Every remaining junction is dropped from `junctionAt` when `creaseLeavesUnfilleted` holds,
   which is true precisely when some crease at the vertex went unfilleted — and the reason,
   size gate or brush or classifier, is not asked.

So no bead is truncated toward a corner cell that will not be built.

Route 2 was measured directly by making `seamVertex` return false in a scratch build and
sweeping the repro's radius over 0.2 … 2.0: the mesh changes (401 vertices against 356 at
`r = 0.5`) so the branch is live, but validity is the same at every radius either way. Route 1
is enough on its own for this model. The scratch build was reverted; the tree carries no
environment variable.

## Found and not fixed

Sweeping the repro's radius turns up **one non-manifold edge at `r` = 0.2, 0.8, 0.9 and 1.0**,
valid at every other radius tried. At `r = 0.9` it is a 0.34 µm sliver from
`(0.440817, 3.10247, 7.5)` to `(0.440817, 3.10213, 7.5)`, carried by four faces, stable across
weld tolerances 1e-4 … 1e-9. That is on the top face at the tangency boundary of the `r = 0.9`
concave bead, `4 − 0.9 = 3.1` in from the crease at `y = 4`, and it is nowhere near either
refusal site. `box_step` swept the same way over `r` = 1 … 5 is clean, so it is the oblique
junction and not refusal that produces it.

It breaks promise 1 and it is not D24. It wants its own number.
