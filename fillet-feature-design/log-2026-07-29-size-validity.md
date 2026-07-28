# 2026-07-29 — the size gate, and the last of the junction work

Two things landed together because the first one kept answering for the second.
The operator now refuses a size the feature cannot carry, one crease at a time,
and the junction work that was left after M8 — mixed-sign vertices, and a corner
the solve refuses — is closed.

## What landed

`smoothSurfaces`, `chainContacts`, `pointTriangleDistance` and `checkChainSizes`
in `fillet::detail`, called from `buildFilletTool` between walking the chains and
building anything. A chain that fails is warned about and dropped; the rest are
built as before. `chainJunctions` now takes its constraint walls from every
triangle at the vertex rather than from the selected creases' own walls, and
`buildRoundSolid` ramps a spine's radius to nothing where the junction it runs
into has no seated ball.

## The gate is two questions, both about contact points

Neither needs a solid built, which is the point: §6.5 argues that computing the
largest size that fits costs about what building the tool and testing it costs,
and a check that had to build the tool first would have inherited exactly that.

**Does the tool still touch the model?** The blend meets each wall at one point
per station, and that point has to land on the wall. The wall is the smooth patch
the crease's triangle belongs to — triangles joined across every edge below the
crease threshold — so a bore is one wall and a cube's faces are six, which is the
distinction the question needs and the one the mesh does not carry. An L whose
faces are 30 mm long asked for `r = 35` puts both contact points 5 mm past the
end of their face, and there is no bead of that radius anywhere near it.

**Is the room it needs its own?** A crease's seated ball with another crease's
contact line inside it is that crease's bead being eaten. This is the "half the
distance to the nearest other feature" constraint, asked in the only place it can
be answered locally. It refuses `r = 30` on a 40 mm cube — each edge reaches back
30 and the edge across the face reaches 30 the other way — and `r = 8` between
two walls standing 10 mm apart, which is the case the suite was carrying with no
opinion at all.

Junction-sharing creases are exempt from the second question. Sharing material at
a vertex is what a corner cell is for, and every corner in the suite would fail
the test otherwise.

## Four things that were not obvious

**A crease has to be sampled between its stations.** The spine's stations are
mesh vertices. A spike has two — its base corner and its apex — and everything
wrong with rounding it at `r = 1` happens between them: the section falls below
the tool's own setback about 27 mm from the tip, and the tool would take the tip
off rather than round it. Neither station sees that. The gate walks each segment
as well, with the walls interpolated the way the cell between two sections
already interpolates them.

How finely has to follow the size, not the mesh: one sample per size along the
segment, never fewer than three, capped at 32 because a crease can be arbitrarily
long beside a tool that is arbitrarily small and the crowding test is quadratic
in its samples. A fixed count is a trap — at `r = 1` three samples catch the
spike 27 mm from its tip, and at `r = 0.5` the place where the room runs out has
moved above the last of them, so the tool ate the top 20 mm and said nothing.

**The two ends of an open chain are not asked the question.** A crease stops at
the boundary of its own walls — at a junction, or where the feature runs out — so
the contact point at that last station sits in the corner of the wall and steps
out of it for reasons that have nothing to do with the size. On a tetrahedron it
is unmissable: the base triangle's corners are 60°, so stepping perpendicular to
one base edge leaves through the next, and every cone in the suite was refused
until the rule went in. A size that genuinely does not fit fails *along* the
crease, and the samples in between are what say so — which is why the L asking
for `r = 35` on 30 mm faces is still refused, ends or no ends.

**A sample belongs to its segment's walls, not to its station's.** Taking the
wall from the station worked until a chain turned a corner: the bead around a
rib's foot is a closed loop, and half of each segment was being measured against
the wall the *previous* segment rode. The overshoot it reported grew linearly
along the segment, which is what a wrong reference plane looks like.

**The slack scales with the distance to the ball centre, not with the radius.**
Where a chain turns, or between two stations, the wall normals the contact is
built from are an average, and the contact lands at the mitre between two walls
rather than on either. The miss is an angle applied at the ball centre, so what
sets its size is how far that centre is from the crease — and on a nearly flat
crease that is many radii (`r / cos(φ/2)`, which at 176° is 28 r). Written in
terms of the radius alone the tolerance was an order of magnitude too small, and
a 60 x 2 cone was refused a round of 0.6 that fits it comfortably. The same
quantity absorbs the tessellation's own error and the "over the limit by less
than float noise, clamp silently" exception §6.5 asks for.

## There is no right sample density, and the search was stopped

Every setting in the gate — how many samples, how close to a junction to stop
asking, how much slack a mitre gets — fixes one case and breaks another when it
is moved. Three samples per segment caught a spike at `r = 1` and missed the same
spike at `r = 0.5`, where the room runs out above the last sample. One sample per
size caught both and started refusing every cone, because the samples now landed
close enough to a base corner that the contact leaves through the neighbouring
crease's face — a corner, not an overshoot. Excluding a stretch either side of a
junction fixed the cones and made that stretch a blind spot.

That is not a defect to tune out; it is what asking a continuous question at
points means. The rule is written down and left alone: one sample per size along
a segment, at least three, capped at 32; no touching question within `2r` of a
junction or at the two ends of an open chain; slack scaled by the distance to the
ball centre. It errs toward accepting, on purpose — a false refusal is a fillet
the user cannot have, a false acceptance is one they can see is wrong.

The exact version is the one §6.5 already prices: build the tool and test it, or
ask a distance field whether the offset surface stays on the model. That is the
replacement if the sampled gate proves too coarse in practice. A finer sample is
not.

## Mixed-sign vertices: a larger constraint set, not a special case

The corner solve collected its walls from the creases it had selected, so a
convex edge arriving at a concave corner was never heard of, and a centre solved
from the concave walls alone could sit buried in a face nobody had mentioned. It
now takes every triangle incident to the vertex. That is a superset of what it
had, so a centre that was feasible and genuinely clear of its neighbourhood is
still feasible — the exact corner comparisons stayed green through the change,
which is the evidence that matters, since they compare against the true rounded
solid rather than against the previous answer.

The four Group A cases were already clean before this. They stay clean; the
difference is that they are now clean by construction rather than because the
unheard-of face happened not to be in the way. The case that would have shown the
hole — a convex edge leaning back *over* a concave one — was never written, and
now there is nothing for it to show.

## Runout: the corner that cannot be closed

A junction whose solve is refused gets no corner cell, and truncation did not ask
before cutting every incident spine back. The needle showed how bad that is: at
`r = 1` its three beads stopped 80 mm below the apex and a sharp spike stood over
the stumps, with no warning.

Those ends are no longer truncated. The radius ramps linearly to zero over the
last `2r` of the spine, the final section is the sharp vertex itself, and the
beads converge on it — §6.3.1's runout, built out of the section constructor
taking a radius per sample instead of one per chain. What comes out is a valid
solid whose blend fades locally, and a warning that says the corner is not the
constant-radius one that was asked for.

**The needle no longer reaches it, and that is the right ordering.** The gate
refuses the three slant creases first, because the spike is thinner than the tool
for its last 27 mm, so only the three base creases are built and the tip survives
untouched. The runout is what happens at an unsolvable corner when the size does
fit; the unit test exercises it directly by calling the builder without the gate
in front. Any apex sharp enough to trip the `|P − v| > 10r` bound is also sharp
enough to fail the touching test somewhere above it, so on this shape the two
will always fire in that order — the runout's real job is the corner that is
refused for the *other* reasons, and it is there for those.

## Unequal radii

Nothing to detect. A tool node carries one size for the whole invocation, so
unequal radii at a vertex cannot be expressed; the detect-and-warn path belongs
with the feature that would create the situation — per-edge sizes, or a radius
varying along a chain — and not before it.

## What the suite says

The five `drop` variants go green on `drops`, which wants an empty tool *and* the
warning, so the catch-all line that excused all of them is gone. Nothing that was
passing stopped: the committed regressions are byte-identical, and the exact
comparisons in `FilletCompare_test.cc` — cube edges and corners, both apexes,
both pockets, the sheared pyramids, the hole mouth, the boss base — are unchanged
through both the constraint-set change and the gate.

The near-wall case is the one whose story changed. It was recorded as the
operator "neither filling the crease nor refusing the size"; it now refuses the
two creases facing each other across the gap and builds the third, which has
nothing within reach. Its large variant stays `none` rather than becoming `drop`,
because a `drop` asserts the whole tool must be empty and one crease there is
legitimately buildable.
