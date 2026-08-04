# 2026-07-29 — the corner cell, rethought

D2 closed with two things it had not fixed, filed as one open item because both
showed up at a junction and both were described as "the corner cell":

- nine triangles of about 1e-13 on the pocket cases' slant beads, at the one
  height each is cut back at, keeping CGAL from dilating them;
- a detached wafer of 5.2e-06 at each junction of `case_two_bosses` at
  `$fn = 48`, which T2 then found and left alone for D2's stated reason.

They are two defects with nothing in common but the place they appear. Probing
the separation the corner cell is hulled at moved the counts without finding a
floor because only one of the two is about that separation at all, and that one
is not about the distance either — it is about which points the cell is built
from.

## The pocket: the cell was reaching with the bead's own section

The corner cell hulls the junction vertex, each corner ball's tangency points,
and a cross-section of every chain that meets there. The section it reached with
was **the chain's own pentagon**, lerped a hair back from where the wedge stops.

A section of a chain is by construction where the cells hulled from it end. So
all five of those points lay exactly on the wedge's surface, and the two solids
touched along the five edges of a shared face before leaving each other — at
whatever angle the cell's faces make with the bead's, which on a spike (one
segment from base to apex, walls that barely turn) is a fraction of a degree.
That is the smallest two faces can differ by anywhere in these tools, which is
why the pocket cases were the extreme of it and why moving the separation only
moved which triangles came out worst.

The fix is to stop reusing that pentagon. `cornerProfile` rebuilds the profile
from the same three corners of the cross-section — the crease point and the two
tangency points — at the corner cell's own distance past the walls rather than
the wedge's, one copy of the crease point per wall. Every point of it is then
`1.5 eps` past a wall where the wedge has at most `eps`, so the two surfaces are
half an eps apart along each wall and cross transversally where the profile turns
the tangency point.

The one direction the profile may **not** grow is outward, into free space no
ball reaches, so its fourth side — the chord between the two tangency points —
comes back toward the crease by `over * cos(phi/2)` instead. That costs nothing.
The chord is not a surface of the blend: it is the far side of a circular segment
lying `r * (1 - sin(phi/2))` inside the arc, which the subtraction removes
whatever the cell does there. The two are comparable only as phi approaches 180
degrees — the crossover is at 179.7 — and `makeRoundSection` already refuses
anything past 179.

Measured on the three-face pocket: smallest triangle 2.4e-13 before, 8.1e-07
after.

## The wafer: the ladder had a rung missing, and tessellation was hiding it

The wafer sits at (29, 29.79, 6) on the two-boss plate, and the corner ball's
centre solves to (29, 29.79, 8) with `r = 2`. It is at the ball's tangency point,
exactly.

A ball seated against a wall touches it at one point. Every cell stands past that
wall — the corner cell by `1.5 eps` — and along a crease the canal cuts that
overshoot back where the bead ends, because every arc carries two points `2 eps`
past its walls. At a corner the cutter is a ball and it carried none, so the
overshoot around the tangency point was reachable by nothing, and the two canals
running into the junction severed it from the rest of the tool on their way past
at a depth the cell's own floor could not match. A wafer, not a sliver: it has
volume, no triangle in it is degenerate, and the only thing that sees it is
counting the pieces.

Why `$fn = 24` was clean and 48 was not: a ball drawn as an inscribed polyhedron
reaches its faces' own distance from the centre, not `r`. On `r = 2` that
shortfall is 0.034 at 24 and 0.0086 at 48, against a corner cell floor of 0.003 —
coarse enough to blunder past the plate and take the overshoot with it, then not.
Measured at the wafer, the ball's surface was 0.0004 *above* the plate at 48.

So the point that stands past the wall has to clear the shortfall first, and
`inradius()` measures it off the ball's mesh rather than assuming it from the
segment count. Worth doing: Manifold's sphere is short by twice
`r * (1 - cos(pi/segs))`, the figure the arcs' chord error is reckoned with,
because its triangles span a facet in both directions rather than one.

## Two things that were tried and are worth not retrying

**A point at `r + 2 eps`, without the shortfall.** The cone it raises is back
above the wall within `sqrt(2 p r)`, which at `p = 2 eps` is 0.016 against a
wafer 0.035 across. It cut the wafer thinner and left it.

**A whole copy of the ball, translated past each seated wall.** This covers the
patch rather than a point, and it is wrong for a reason worth writing down: it
moves the ball's surface *near the wall* by the whole push, while the canals it
hands over to do not move at all. The corner is then recessed by the tessellation
shortfall and steps against every bead arriving at it — the three-face pocket's
tool lost 0.4 % of its volume, 3709.3 to 3694.3, where the point leaves it at
3710.0. The re-read rounded cube reported 24 creases at its eight corners on a
solid that has none, three to a corner, which is one per bead meeting each. A
point does not do this: it only deepens the cut where the cone is still below the
wall, and above the wall the surface is the ball's own.

## What this leaves

`case_junction3_pocket` and `case_junction4_pocket` are still red on `sandwich`,
and their ledger entry is rewritten for the third time rather than deleted. What
CGAL says has moved with the geometry at each of the three fixes, which is the
evidence each reached what it was aimed at: it used to throw out of the Nef
conversion of the tool itself, and that conversion now succeeds — the refusal has
moved downstream into the convex decomposition, which reports two facets sharing
a halfedge. Nothing measured says that one is the operator's.

The re-read cube improves again without being aimed at: 480 features and 288
spurious concave ones become 360 and 216, and the shortest edge a spurious crease
sits on goes 1.3e-3 to 5.6e-3 on a 40 mm part. `FilletBuilder_test.cc` carries
that as a bound, which has now moved twice and is written where the measurement
is.

## Tests

`FilletCompare_test.cc` gains one case and one assertion, both of which fail on
the code before this and neither of which needs CGAL:

- *a corner sheds nothing of what it stands past its walls* — the two bosses at
  `$fn = 48`, one piece of genus 1. Before: three pieces, genus -1.
- the pocket tool's **smallest** triangle area, next to the existing count of
  triangles with none. The count could not see 1e-13.

`case_two_bosses` stays at `$fn = 24`: the 48-gon shape is now a unit test that
costs no dilation, and the case's own question was always topological.
