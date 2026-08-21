# Junction cases — the queue, and what it found

This file was a queue of corner configurations the operator had never been
pointed at. They are built now, one case file each, and this is the record of
what each one turned out to do. The detail lives in the case headers, which is
where it stays correct; what follows is the shape of the answer and the reason to
have asked.

Read [`README.md`](README.md) first: it has the case-file contract, the four
checks, and what `ref` / `none` / `drop` mean. Everything here assumes it.

Every case here is an equal-radius blend at a vertex, so none of them has an
elementary closed form and all of them are `none` — `sandwich`, `emits` and the
picture are the coverage. A result recorded in `expectations.txt` says which of
the two kinds of red it is: the *operator* being incomplete, or the *check* being
unable to express the answer. The difference matters, and lines of the second
kind will still be there when the operator is finished.

---

## Group A — vertices where creases of both signs meet

`case_junction_mixed_rib` / `_round` — a rib on a plate, free at both ends, with
two concave creases and one convex edge meeting at `(10, 18, 6)`.
`case_junction_mixed_notch` / `_round` — a notch cut into a block's corner, with
the mixed vertex where it opens through the top face and an all-concave floor
corner in the same picture.

**The hole probed.** The corner solve collects its constraint walls from the
*selected* creases only, so a convex edge arriving at a concave junction is never
heard of even though its far face bounds where the ball may sit. The sign flag
applied to the whole junction is meaningless there too. Expected: gouges, found
by eye rather than by check, since a gouge at one corner is well within what
`sandwich` tolerates.

**Found: all four are clean**, and each carries no expectation line. The bead
turns the corner as a closed loop around the rib's foot with nothing beyond the
end face; the notch's crease is filled to its full height with nothing above the
top face; neither round tool puts material where the concave side is.

That was a weaker result than it looked: the unheard-of face has to actually be
in the ball's way for the hole to show, and in both models it is a face the
selected creases already constrain the ball against. The hole is closed anyway,
and not by a case — the corner solve now takes its constraint walls from **every
triangle at the vertex** rather than from the walls of the creases it selected.
That is a superset of what it had, so nothing feasible and genuinely clear has
moved, which the exact corner comparisons confirm by staying green. These four
stay clean, now by construction; the case nobody wrote down — a convex edge
leaning back over a concave one — has nothing left to show.

## Group B — junctions the solve refuses, where the spine is cut anyway

`case_junction_needle` — a 3 x 120 spike whose seated ball trips the
`|P - v| > 10r` bound. `case_junction_flat_apex` — a 60 x 2 cone, the probe for
the other rejection path.

**The hole probed.** Truncation works off the incident wall normals and never
asks whether a corner cell was built, so a refused solve still gets every
incident spine cut back with nothing filling the space.

**Found, on the needle: exactly that, and it was severe.** At `r = 1` the three
beads stopped 80 mm below the apex — eighty radii — with a fully sharp spike left
standing over the stumps and no warning. The result was at least a valid solid.

**The runout fallback is built now.** An end whose junction has no seated ball is
not truncated at all: the radius ramps to zero over the last `2r` of the spine,
the last section is the sharp vertex itself, and the beads converge on it — a
valid solid whose blend fades out locally, with a warning saying the corner is
not the constant-radius one that was asked for.

The needle no longer reaches it. The size gate refuses its three slant creases
first, because the spike is thinner than the tool's own setback for its last
27 mm, so only the base creases are built and the tip survives. That ordering is
right — whether a size can be built here at all comes before what happens at a
corner nobody can seat a ball in — and it means the runout's acceptance is the
unit test rather than this case, since the test calls the builder without the
gate in front. Any apex sharp enough to trip the `10r` bound is also too thin for
the size somewhere above it, so on this shape the two will always fire in that
order.

**Found, on the flat apex: the guard does not fire, and cannot.** The determinant
is around `1e-2` against a `1e-6` threshold; reaching the threshold on a cone
needs a slant within about `0.04°` of the base, which no radius fits inside. It
is red only in `sandwich`, and for a reason that is about the check: what is flat
here is also sharp, and rounding the 1.9° knife edge at the rim sets the surface
back some sixty radii while being entirely correct. So
the determinant guard is unreachable by anything buildable, and the open question
is whether a scale-free threshold that says nothing about `r` guards the right
quantity at all — `|P - v| > 10r` is the one that asks whether the solved centre
is *usable*, and it is the one the needle reaches.

Only the `10r` path is reachable by a valence-three corner, and there is a reason
of principle: three *distinct* planes that fail to pin a point down would have to
share a direction, and three planes sharing a direction and a point share the
whole line through it — an edge, not a vertex. The determinant guard is defensive
against near-degeneracy and against a bad triple at valence four and up.

## Group C — a crease that runs *past* a face without meeting it as a crease

`case_near_wall_fillet` — a concave crease along a floor with a second wall
standing 10 mm away, parallel and not touching. Two sizes: `r = 3` fits the gap,
`r = 8` does not.

**The hole probed.** Truncation happens at junctions, and there is no junction
here — no shared vertex, no crease between the bead and the wall it approaches —
so nothing stops the ball rolling straight through that wall.

**Found: the opposite failure.** The ball never entered the second wall. The bead
*collapsed* instead: at `r = 8` the first wall's fillet was 0.26 mm tall where it
should be 8, and deleting the second wall from the same model restored it — the
neighbour crushing it not by rolling through the wall but by rolling its own ball
through this bead from the other side. Nothing caught it: `sandwich` bounds only
how far the result may STRAY from the model and an under-fill strays nowhere,
`emits` was satisfied by the sliver, and no warning was emitted.

**It is refused now**, which is what this case was for: it is where "refuse" got
settled against something real. A crease whose seated ball contains another
crease's contact line is competing for material already spoken for — the "nearest
other feature" half of the size limit — so at `r = 8` both creases across the gap
are dropped with a warning and the third, with nothing within reach, is built. At
`r = 3` all three are built, since the two beads have 4 mm between them.

The large variant stays `none` rather than becoming `drop`: a `drop` asserts the
whole tool must come out empty, and that third crease is legitimately buildable.

## Group D — asymmetric high valence, in the picture

`case_junction5_apex_skew` / `case_junction5_pocket_skew` — a sheared five-sided
pyramid, and the same shape as a pocket.

These add nothing to correctness: `FilletCompare_test.cc` already compares a
sheared pyramid's apex against the exact rounded solid at four, five and six
sides, both signs, and checks that the apex yields 2, 3 and 4 distinct ball
centres rather than collapsing to one. What they add is the contour stack, which
is the only way to *see* a multi-centre corner — and the shear is what makes it
visible, since on a symmetric cone every centre coincides and a collapsed corner
draws the same picture as a correct one.

Both are red for reasons that already have lines in `expectations.txt` and belong
to the checks rather than to the corners: the apex cannot pass `sandwich`,
the pocket cannot be dilated by CGAL at all.

`r = 2` and no larger. This shape has a crease at 130°, where the tangency
setback is over twice the radius; past about 2 on a 30 mm feature the
neighbouring beads collide and the result stops being the rounded solid. That is
the oversize question, and mixing it in here would hide both.

---

## What not to build here

- **Unequal radii per edge.** Out of scope by design — the whole offset framing
  collapses. It needs nothing yet either: a tool node carries one size for the
  whole invocation, so unequal radii at a vertex cannot be expressed, and the
  detect-and-warn path belongs with the feature that would create the situation.
- **Oversize radii in general.** "Refuse" is settled and the five `drop` variants
  in the older cases carry it. `case_near_wall_fillet` stays `none` for a
  different reason — see above.
- **Anything with a hand-written reference.** None of these has an elementary
  closed form. `none` is the honest kind; `sandwich` plus the picture is the
  coverage.
