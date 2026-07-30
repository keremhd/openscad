# Fillet Operator — Remaining Work

**This document supersedes the milestone list for everything still to do.**
[`detailed-milestones.md`](detailed-milestones.md) is now a record of what
landed and why; [`fillet-operator-plan.md`](fillet-operator-plan.md) is still the
design. Where either of them describes future work that is not listed here, this
document wins — several of their open items have been deliberately cancelled,
and re-opening one because a milestone table still mentions it would be a step
backwards. The cancellations are listed at the bottom with their reasons.

The convention in `detailed-milestones.md` still holds and applies to everything
below: **no design-doc references in the source.** Do not cite this file, a
milestone number, or a plan section from a `.cc`/`.h` comment. Explain the
reasoning inline instead.

M0–M10 and the size-validity gate are landed. What follows is the whole of what
is left to call the feature complete.

---

## Order of work

Roughly dependency-ordered; the groupings are what matter more than the sequence.

1. ~~**D4, D5, T1**~~ — **done.** T1 was already in the tree; see below for what
   it changed.
2. ~~**D1 and D6**~~ — **done.** See below for what the nearest-point route did
   and did not replace.
3. ~~**D2**~~ — **done.** The cause was not the one written down; see below, and
   [`log-2026-07-29-d2.md`](log-2026-07-29-d2.md). What it left behind read as
   one open item about the corner cell and was two unrelated defects; both are
   now closed, see [`log-2026-07-29-corner-cell.md`](log-2026-07-29-corner-cell.md).
   The two `sandwich` lines are still red, on a third thing.
4. ~~**M12**~~ — **done.** The C++ node, as overruled; see below for the one
   thing the design did not anticipate, and for the parameter it gained.
5. ~~**T2**~~ — **done.** Both cases built; the curved junction exists and is
   solved, and the dome's oversize refusal is not the one that was predicted.
   See below.
6. ~~**D7**~~ — **done.** The overloaded sentinel is split at the caller; the
   junction path was already right. See below for the table after the fix.
7. ~~**D8**~~ — **done.** The crowding question is asked of the corner the blend
   occupies, not of the seated ball; a cube now takes every radius up to half its
   side. See below for what the sketch did not say about sharp creases.
8. ~~**D9**~~ — **done.** A corner is built only where the brush covers the
   setback down every edge of it. The beads there are still built; dropping
   those too is BRUSH-WIDTH's row 2, which reuses this rule.
9. ~~**D10**~~ — **done, and it was not a width threshold.** A spine that meets a
   brush face exactly on the line between its two triangles could be caught by
   neither. See below.
10. ~~**BRUSH-WIDTH**~~ — **done, with one row of its table changed and the
   warning scoped.** The length test is scoped to the stretches the brush cut,
   and a stretch reaching a corner it does not cover is dropped with the corner.
   See below, and [`log-2026-07-30-brush-width.md`](log-2026-07-30-brush-width.md),
   which also records D9 and D10 re-measured.
11. ~~**D11**~~ — **done, and the warning was naming the wrong crease.** An L
   bracket's reflex crease was never refused; what `4.4e-16` dropped was the
   convex chain running the end face's outline, where the spine turns from one
   wall onto the next. See below.
12. **D12** — a concave bead that runs out to an open face leaves its end
   cross-section standing as a sharp lip over the rounded outline beside it,
   because both tools are built from the same original child. The obvious
   composition fix — run the round tool on the already-filleted solid — is
   strictly better on polyhedral models and destroys every curved one, and
   `min_angle` does not separate the two cases. What made it destroy them is
   fixed — the overshoot is measured against the wall it stands past instead of
   being a fixed hair — but the lip itself is still there, and the composition
   that closes it is still to be chosen. See below, including what it costs the
   quoted SCAD equivalent.
13. **DOC**, then **CLEAN**.

---

## D4 — one shared feature test, and ties are rejected — **DONE**

The dihedral-versus-threshold comparison appears at four sites — `classifyEdges`,
`selectedEdges`, `smoothSurfaces` and the debug colouring — written out
independently as `if (ec.dihedralDeg < thresholdDeg) continue;`. Two problems in
one.

**They can disagree.** `smoothSurfaces` uses the opposite sense to group faces
into surfaces, so at a value where the four do not agree, an edge is a crease to
one and a smooth seam to another.

**An exact tie is reachable and breaks on float noise.** The threshold is
`1.5 x` the caller's facet angle, so a model tessellated at **two thirds** the
caller's `$fn` turns by exactly it — `$fn = 8/12`, `16/24`, `32/48`. Measured:
`cylinder($fn = 16)` under a caller at `$fn = 24` has all sixteen vertical edges
turn 22.5 degrees against a threshold of 22.5, the computed dihedrals land a few
ulp either side, and **12 of the 16 are rounded while 4 are left sharp**. Nothing
about the model distinguishes those four.

**Do:** one shared helper, used by all four sites, rejecting on tie:

```cpp
inline bool isFeatureAngle(double dihedralDeg, double thresholdDeg)
{
  return dihedralDeg >= thresholdDeg * (1 + 1e-9);
}
```

Ties reject rather than accept, because the rule is "an edge is a feature when it
turns *more* than the tessellation's own facets do" and equal is not more — and
because the wrong answer in that direction leaves a prism a prism, where the
other rounds every facet of one. `1e-9` relative is about `2e-8` degrees: four
orders above the ulp noise (`~4e-15`) and far below any decision made on purpose.

Comment to carry at the helper, short and free of doc references:

```cpp
// Ties are rejected, not accepted. An exact tie is reachable — the threshold is
// 1.5x the caller's facet angle, so a model tessellated at two thirds the
// caller's $fn turns by exactly it — and there the dihedrals land a few ulp
// either side of the threshold, splitting edges that are identical by symmetry.
// Rejecting keeps a prism a prism; accepting would round every facet of one.
```

**Acceptance:** a unit test on `cylinder($fn = 16)` under a `$fn = 24` caller
selects the two rims and none of the sixteen vertical edges, and the count is the
same on repeated runs. The three rows already probed stay as they are: `$fn = 12`
takes all 12 vertical edges, `$fn = 32` takes none.

---

## D5 — say something when nothing is selected — **DONE**

Three paths today, none of them good:

| Situation | Today |
|---|---|
| No creases of the wanted sign, no brush | Silent. Empty tool, no message. |
| No creases of the wanted sign, brush present | `"the selection brush covers none of the 0 candidate edge(s)"` — blames the brush for the solid having no creases |
| Creases existed, the size gate dropped them all, brush present | The correct per-crease warnings, then a **second** spurious one blaming the brush |

The second and third happen because `candidates` counts the segments of `usable`,
which is the list *after* the size gate. And the case that will actually reach
users is the sign: `fillet_tool` on a plain cube selects zero concave edges and
says nothing, when they meant `round_tool`. The counts are in hand — the echo
line already prints `concave 0, convex 12` — it is only that nothing warns.

**Do:** two edits.

1. Immediately after `classifyEdges`, warn whenever the selected count is zero,
   independent of any brush, naming the sign and the opposite sign's count:

   ```
   fillet_tool: no concave edge of the target turns more than 18.0 deg; nothing
                is built. The target has 12 convex edge(s) — round_tool takes
                those. min_angle= lowers the threshold.
   ```

2. Guard the existing brush warning on `candidates > 0`, so it fires only when
   there really were creases for the brush to miss.

**Acceptance:** `round_tool(r=1) sphere(10, $fn=32)` warns (zero features of
either sign). `fillet_tool(r=1) cube()` warns and names `round_tool`. A brush
that misses a real crease still gets the brush warning. A model whose creases are
all refused by the size gate gets the per-crease warnings and **no** brush
warning.

---

## T1 — DONE, and it moved D2

Both pins already exist in `FilletBuilder_test.cc` — `"refillet: a rounded solid
re-read carries creases its shape does not have"` and `"non-manifold input:
shared edges are counted and nothing crashes"`. Nothing to write.

**Non-manifold input behaves as expected.** An edge with four incident triangles
is counted and declined; two cubes sharing a vertex have no non-manifold edge at
all and are simply rounded independently. The tool builds on both.

**Re-fillet does not fail the way this document predicted, and the correction
matters.** The prediction was that a fillet tessellated at one `$fn` and re-read
at another would have its own arc facets read as creases — a `min_angle=`
problem. That is not what happens. The arc facets fall under the threshold
exactly as they should; what comes back instead is **hundreds of creases of both
signs on a solid that is convex everywhere**, and they are not a property of the
shape: the exact rounded cube, built as the hull of eight spheres, classifies at
zero features, and the tool's own result is within a fraction of a percent of its
volume. The mesh is what is wrong. The beads meet the flat faces and each other
tangentially, and a tangential meeting triangulates into slivers whose normals
are numerical noise — on a 40 mm part the spurious creases sit on edges four
orders of magnitude smaller.

**Those are the same slivers as D2**, measured from the other end. There they
stop CGAL dilating a filleted pocket; here they make the operator's own
classifier disagree with the shape it just built.

**Re-run after D2 landed, as that section asked.** The counts move and do not
collapse: 650 features and 383 spurious concave ones become 480 and 288, and the
shortest edge a spurious crease sits on doubles, from 6.6e-4 to 1.3e-3 on a
40 mm part. A quarter of the noise was the tool's own sliver strip and is gone
with it. The rest is not the tool's to remove: the bead really does meet the
flat face tangentially in the *applied* result, which is what a fillet is, so
any tessellation of it triangulates into something whose normals are noise. This
is a re-read problem, and `min_angle=` is the answer that ships for it.

---

## D1 + D6 — the size gate's contact test — **DONE**

Both lived in `chainContacts` and were done in one pass. What landed, and the
one place it stops short of the sketch below:

- **D1.** The brush block now runs *before* the size gate, so the gate is asked
  about the chains as the brushes left them, and `chainContacts` answers only at
  samples inside a kept interval — points outside one come back as invalid
  placeholders, keeping the ends of the list the ends of the chain for the
  callers that exempt them. The comment arguing the gate belongs first is
  replaced by the reasoning above. The spike brushed to its lower half now
  builds; brushed to its upper half it is still refused, and at a sample the
  brush kept rather than wherever on the crease the room first ran out.
- **D6.** `TA`/`TB` are the nearest points on their walls to the ball centre, and
  the off-face question is the exact one: is that point inside the wall, or on
  the wall's rim? `mitreSlack` and the face tolerance are gone from that test
  along with the tangent-plane construction, which now only supplies the *number*
  the warning reports — how far past the wall's end the blend would stop, which
  is what the L-arm test pins at 5. The dome builds at every radius probed; the
  boss 1.5 mm from a plate edge is still refused at `r = 2` and builds at 1.4;
  every existing `drop` variant and every size test is unchanged.
- **Not done: crowding from the same query.** It still asks other chains'
  contact lines, not the whole mesh. The junction exemptions that test gets from
  chain adjacency have no equivalent in a distance field — at any convex corner
  the third wall sits inside the ball — so "free" it is not, and the Group 2 gap
  it would close has no reachable failure (see `next-cases.md`). The corner
  exemptions in the off-face test were kept for the same reason; the sketch
  below guessed they would go, and nothing measured says they can.

The rest of this section is what was reasoned out beforehand, kept because the
measurements in it are the argument for the route that was taken.

### What `chainContacts` is, before the two bugs

The size gate answers "can a blend of this size be built along this crease at
all" **without building it** — building it and testing the result is the exact
answer and is what the design declined to pay for. It does that by reasoning
about the *seated ball* rather than about volumes.

At a point along a crease, seat a ball of the requested radius into the corner so
it touches both walls. That gives three points — the ball's centre `C`, and the
two tangency points `TA` and `TB` where it meets wall A and wall B. `TA` and `TB`
are exactly where the finished blend would stop being a blend and become the flat
wall again. `chainContacts` walks a chain and returns one such `ChainContact` per
sample: `C`, `TA`, `TB`, which smooth surface each of the two walls belongs to,
and how much slack that sample is allowed.

`checkChainSizes` then asks two questions of those contacts, and a chain that
fails either is dropped with a warning:

1. **Does the tool still touch the model?** `TA` has to land on wall A and `TB`
   on wall B. If a contact point falls off the end of its wall, there is no blend
   of that size there — the arms of a 30 mm L cannot carry `r = 35`.
2. **Is the room it needs its own?** If another crease's contact line sits inside
   this crease's seated ball, the two are competing for the same material. That
   is what refuses `r = 30` on a 40 mm cube.

"Which wall" is a whole smooth surface, not one triangle: `smoothSurfaces` joins
triangles across every edge that is not a crease, so a bore's facets are one wall
and a cube's face is another. Sampling is one sample per size along a segment, at
least three and at most 32, with junctions excluded — a crease is sampled along
its length rather than only at its mesh vertices, because on a tapering feature
the room runs out between two stations.

Both bugs below are in how a contact point is placed and which samples get asked.

### D1 — the gate does not know about the brush

The size gate runs before brush selection and on the whole chain, so a crease
that cannot carry the size is refused **even where the brush selects only the
part of it that has room**, and the warning fires for creases the user never
selected. `chainContacts` has to read `Chain::keep` and ask its two questions
only at samples inside a kept interval.

Note the comment currently sitting above the brush block, which argues the gate
belongs first: *"the gate's question is about the crease and the model around it,
and selecting half of one does not make the size fit there."* That is true of the
**crowding** question and false of the **off-face** one. Selecting half a crease
genuinely does change whether the blend leaves the surface it is meant to meet,
because the part that leaves it is no longer being built. Update that comment
along with the code rather than leaving it to contradict the behaviour.

### D6 — a curved wall fools the off-face test

**Confirmed by probe, and it is a false refusal — the failure direction the size
design explicitly errs against.**

```openscad
fillet_tool(r=1) union() { cube([40,40,10]); translate([20,20,10]) sphere(r=8,$fn=64); }
```

Classification is correct: 64 concave edges, one closed ring, sphere facets
rejected at 18 degrees. Then the gate refuses it:

```
WARNING: fillet_tool: radius 1 does not fit the crease at [12.03, 19.61, 10]
         - the blend would leave the surface it is meant to meet, by 0.0594703
Current top level object is empty.
```

`r = 1` on an 8 mm dome standing on a 40 mm plate plainly fits. Measured, with a
cylindrical boss of the same radius 8 as the control:

| | r = 0.3 | r = 0.5 | r = 1 | r = 2 |
|---|---|---|---|---|
| sphere dome, R = 8 | passes | passes | **refused by 0.0595** | **refused by 0.2366** |
| cylinder boss, R = 8 | passes | passes | passes | — |

The overshoot is the **sagitta**, `r^2 / 2R`: 0.0625 at `r = 1` and 0.25 at
`r = 2`, against 0.0595 and 0.2366 measured — the difference being the tolerance
already allowed. The contact point is placed by stepping along the tangent plane
at the station, and a doubly-curved wall leaves that plane by exactly that
amount. The cylinder does not trip it because the step runs along the ruling,
where there is no curvature.

The existing tolerance scales with the distance to the ball centre, which absorbs
the mitre error of averaged normals and the tessellation's own error, but carries
nothing about the wall's curvature.

**Do not fix this by enlarging the tolerance.** A tolerance big enough to swallow
the sagitta at `r = 2` is 0.25 mm of slack handed to every crease in every model,
which gives back the refusals the gate exists to make.

#### This is not the tapered-stick problem, and the difference decides the fix

Worth separating, because both defects are in the off-face test and the two ask
for opposite things.

The needle and the `$fn = 3` cone are a **sampling-density** problem: each sample
is correct, and the room runs out *between* two of them. The answer there was to
sample more finely, which is why the walk scales with the size.

D6 is a **per-sample construction** problem: sampling has nothing to do with it.
Every sample on a dome is wrong, in the same direction, by the same
`r^2 / 2R`. Sampling a thousand times gets the same refusal, because the error is
in how each contact point is built:

```cpp
c.C  = s.v + dir * (r / cos(phi/2)) * bis;
c.TA = c.C - dir * r * s.nA;     // stepped off C along a vertex-averaged normal
c.TB = c.C - dir * r * s.nB;
```

`TA` and `TB` are *constructed* by stepping off the ball centre along averaged
wall normals, and `pointTriangleDistance` then measures how far that constructed
point sits from the wall. The measurement is exact. The construction assumes the
wall is flat, so on a curved wall the point it produces is genuinely off the
surface and the gate genuinely reports it.

Note that `mitreSlack` already exists as a tolerance for this same species of
error from a different source — where a chain bends, the averaged normals put the
contact in the air at the mitre between two walls. Curvature is the second source
and the slack was only ever calibrated for the first. Adding a curvature term to
the slack would be the third patch on a construction that should not need one.

#### The exact route, which is the analytic idea in its cheap form

Stop constructing the contact point. **Take `TA` as the nearest point on wall A's
triangles to the ball centre `C`.** Then it lies on the wall by construction, on a
dome as much as on a plate, and no curvature term or tolerance is involved.

Both of the gate's questions fall out of that one query, exactly:

- **Off-face** becomes: is the nearest point in the *interior* of the wall, or on
  the wall's boundary? A ball whose nearest contact is a boundary edge is hanging
  off the end of that wall — which is precisely what "the blend would leave the
  surface it is meant to meet" is trying to say, stated exactly instead of as a
  distance against a tolerance.
- **Crowding** comes free from the distance itself: `|C - TA|` should equal `r`.
  Less than `r` means something is in the way. Asked against the whole mesh
  rather than only against other creases' contact lines, this also closes the
  "obstacles that are faces, not creases" gap that the old case queue's Group 2
  was written for — a face can block a ball without carrying any crease.

**The machinery already exists.** `pointTriangleDistance` is in the internal
header, and `BrushVolume` in `FilletBrush.{h,cc}` already builds an AABB tree over
mesh triangles with exactly the traversal this needs. This is the plan's "ask a
distance field whether the offset surface stays on the model" in its cheap,
concrete form — an exact query against the mesh, not the expensive "build the
tool and test it" the plan priced.

**What it removes:** the tangent-plane approximation, `mitreSlack` and its
calibration, and most likely the corner exemptions — a nearest point landing on a
wall boundary near a junction is correct information rather than the misreading
the exemptions were invented to suppress.

**What it does not remove: sampling.** The questions are still asked at discrete
points along a crease, so the tapered-stick reasoning and the size-scaled walk
stay exactly as they are. Removing sampling altogether needs the continuous
formulation, which is a much larger job and is not proposed here. Do not conflate
the two while implementing.

Verify against the body of `chainContacts` before committing to this — the
sketch above is read off the construction, not from having built it.

**Acceptance:** the dome above builds at `r = 1` and `r = 2`. Every existing
`drop` variant stays refused, and for the same stated reason — the gate must not
buy this by getting looser. `case_round_past_boss` (a boss 1.5 mm from a plate
edge, correctly refused at `r = 2`) is the specific one to watch, since it is the
nearest neighbour of a false acceptance.

---

## D2 — the bare rounded tool will not convert to Nef — **DONE, and it was not
the corners**

The two disabled CTests are re-enabled and pass. The `sandwich` lines in
`fillet-tests/` are all still there, and the measurement below says why: they
were never this bug. Their ledger entry has been rewritten rather than deleted.

**The diagnosis in the paragraph this section used to open with was wrong.** It
was not the corner spheres, and it was not "too small to survive quantising"
either — CGAL's kernel is exact and never quantises anything. What it does is
refuse a mesh whose triangles enclose no area, and the tool had those by the
dozen along every crease it rounded. A cube's tool had 25 of them; a cylinder's,
which has closed chains and therefore no junctions at all, had none, and
converted cleanly all along. The corners were where they were *counted*, not
where they came from.

Where they came from: the arc and the wedge stop at different distances from the
wall. The wedge stands `eps` past it so the caller's boolean has something to
cut; the arc is tangent, so it reaches the wall exactly and no further. Between
the two, all along the crease, is a strip of the wedge's own overshoot — `eps`
thick, running out to nothing at either end where the arc curves away. That
strip is the sliver, and refining the arc only makes it thinner: it is the gap
between a tangent and its tangent plane. Junctions are where it acquires extra
vertices along its length, from the corner cells cutting the crease short, and
three collinear points on the edge of a strip that thin is a triangle with no
area in it.

**What landed** is a ladder of overshoots, each piece of the tool standing
further past a wall than the piece it has to cut through, so that a cut always
crosses a face and never arrives along it:

| | past each wall |
|---|---|
| wedge cells | `eps` |
| corner cells | `1.5 eps` |
| what is subtracted — the arc | `2 eps` |

The arc's is two extra hull points per section, in the two tangency directions.
The arc itself is untouched: it is not a larger ball, and the blend still meets
each wall where a ball of exactly `r` touches it, to within the overshoot the
tool already carries there. That is the difference from the alternative this
section rejected — growing the ball by an eps loses tangency everywhere and
moves the blend; two hull points at the ends of the arc move nothing the caller
can see.

The corner cell needed two changes to sit in that ladder. Its overshoot had been
deliberately *smaller* than the wedge's, on the argument that matching it would
put two faces in one plane; smaller turns out to be the worse of the two, since
the face then grazes just beneath rather than crossing. And its vertex point was
displaced once along the bisector, which is short of the wall plane by the
cosine — enough to tilt the cell's whole wall face and leave a flake of the
model unblended along the junction, at a thickness that scaled with the
overshoot. One copy of the vertex per wall, each along its own normal, and the
face is that wall's plane exactly. The flake had been there all along, just
under the exact comparisons' threshold; it is gone now rather than merely small.

**What it did not fix, and what that turned out to be — CLOSED.** D2 left two
things behind, and they read as one item because both were "the corner cell".
They were two, with nothing in common but the place they showed up.

*The pocket's nine triangles of 1e-13*, at the one height on each slant bead
where it is cut back for the apex corner. The corner cell reached down each chain
with **that chain's own section**, and a section of a chain is by construction
where the cells hulled from it end — so five of the cell's points lay exactly on
the wedge's surface, and the two solids touched along the five edges of a shared
face before leaving each other at a fraction of a degree. A spike is where they
can differ by least. The cell now reaches with a profile rebuilt at its own
distance past the walls: the same three corners of the cross-section, taken at
`1.5 eps` instead of `eps`, one copy of the crease point per wall, so the whole
profile is strictly further past each wall than anything the wedge has there and
the two cross transversally instead of grazing. Its fourth side comes *back*
toward the crease by `over * cos(phi/2)`, which costs nothing: that side is a
chord lying `r * (1 - sin(phi/2))` inside the arc, and the subtraction takes that
segment away whatever the cell does with it. Smallest triangle in the pocket
tool: 8.1e-07, where it was 2.4e-13.

*The two-boss wafer*, 5.2e-06 of volume on the plate face at each junction of
`case_two_bosses` at `$fn = 48`. Not the corner cell's shape at all. It sits
exactly where the corner ball touches the plate, and **a ball touches a wall it
is seated against at one point** — so the overshoot the corner cell stands past
that wall around it had no cutter, and the two canals running into the junction
cut it free of the rest as they went past at `2 eps`. The ladder had a rung
missing: every canal carries two points past its walls, and the corner ball,
which is what cuts where the canals stop, carried none. It does now — one point
per seated wall, `2 eps` past the tangency, plus whatever the *tessellation* is
short by, which is the part that had been hiding this. A ball drawn as an
inscribed polyhedron reaches its faces' own distance from the centre, not `r`, so
at `$fn = 24` it blundered past the plate by its own coarseness and there was no
wafer, and at 48 it did not. That shortfall is measured off the ball's mesh
rather than assumed from the segment count.

Neither change moves the blend. The profile only grows past walls, and past a
wall is inside the model or outside it, never blend. The ball's point only
deepens the cut where the cone it raises is still below the wall; above the wall
the surface is the ball's own, so the corner does not step away from the canals
it hands over to — which a whole translated copy of the ball did, by the
tessellation shortfall, and that showed up as a fresh crease at every junction of
a re-read rounded cube.

**What is still red, and it is a third thing.** The two pocket cases still fail
`sandwich`. What CGAL says has moved with the geometry each time, which is how
each of these was known to have reached what it was aimed at: it used to throw
out of the Nef conversion of the tool, and now that conversion succeeds and the
refusal has moved downstream into the convex decomposition, which reports two
facets sharing a halfedge. Nothing measured says that one is the operator's.

### A second filter on face size cannot substitute for this — measured

The obvious cheaper alternative is to leave the mesh alone and filter the
*classifier* instead: reject an edge as a feature when the triangles it sits on
are too small or too slivery to trust their normals, on top of the angle test.
It was probed properly and **it does not work**. Recording the numbers so nobody
spends the day rediscovering it.

First, a correction to what the refillet test appears to show: on a rounded cube
**every** feature the classifier reports is spurious, of both signs, so that mesh
cannot answer whether a filter discriminates — there is nothing real in it to
keep. The discriminating case is a rounded cube seated on a plate, where the seam
between them is a genuine concave crease and everything else is noise. Measured
there, over the concave features (52 real, 216 noise), with quality as
`4*sqrt(3)*Area / sum(edge^2)` — 1 for equilateral, 0 for degenerate:

| | real crease | noise slivers |
|---|---|---|
| edge length — min | 6.583e-04 | 6.583e-04 |
| edge length — p10 | 6.583e-04 | 3.556e-03 |
| edge length — p50 | 1.300e+00 | 5.658e-03 |
| quality — min | 2.781e-05 | 1.249e-05 |
| quality — p50 | **8.693e-04** | **6.143e-03** |
| triangle area — min | 1.646e-06 | 1.337e-06 |
| triangle area — p50 | 4.243e-04 | 3.291e-06 |
| triangle area — max | 2.999e+02 | 4.226e-04 |

The two populations do not merely overlap, they are **inverted** on the measure
that was supposed to separate them: the real crease's triangles are seven times
*worse* in quality at the median than the noise is. All three metrics share a
floor to three or four significant figures, and the real crease's 10th percentile
edge is *shorter* than the noise's. Any cut on length, area or quality removes
part of the real crease before it removes most of the noise.

The reason is structural rather than a matter of picking the metric better. The
real crease runs right up against the bead, and near that meeting its own
triangles are shredded by the same tangential contact that produces the noise. A
feature inherits the worst geometry in the mesh exactly where it touches the
thing that made it.

And a partially removed crease is worse than no filter at all: it fragments a
chain into arcs, which changes which vertices are junctions and caps beads in the
middle of a smooth seam.

**So the configurability question does not arise** — it is not that a fixed
constant needs a `min_area=` beside `min_angle=`, it is that no value of such a
parameter separates the two populations. Reject this line; fix the mesh at the
source.

**Acceptance, and what of it was met.** The two disabled CTests are re-enabled
and pass, and their `render-cgal` baseline is regenerated. The exact comparisons
in `FilletCompare_test.cc` are green and untouched, and one test is added there:
the tools' meshes must contain no triangle without area, on both signs, which
fails on the code before this and is the whole defect in one number. The pocket
and apex `sandwich` lines did **not** go green and are still listed, for the
reason above — measured, not assumed.

The circular-segment rewrite stays cancelled (see Cancelled, below). It was
listed as the fallback route to this, and this did not need it.

---

## M12 — the `fillet()` wrapper — **DONE**

Built as described, with one correction the acceptance criteria forced and one
parameter deliberately left off. Details in
[`log-2026-07-29-m12.md`](log-2026-07-29-m12.md); the two that change what
somebody would do next:

- **The dump runs the preview renderer**, so the `.csg` line this section asks
  for cannot exist for a node that passes through under `$preview`. The
  regression file therefore carries a fourth model at `disable_preview = false`,
  which is the only invocation that survives into the dump — and is also the
  acceptance criterion for the opt-out, so one model does both.
- **`min_angle=` is on `fillet()` too**, beyond the four parameters named below.
  The DOC section calls it *the* escape hatch when the threshold picks the wrong
  edges, and an escape hatch reachable only by abandoning the entry point and
  rewriting the composition by hand is not one. It goes to both halves: the
  threshold says which edges of the target are features, and that cannot depend
  on which sign is being built.

The rest of this section is what was reasoned out beforehand, kept because it is
the argument for the shape that was built.

The plan (section 2.1) says ship this as a bundled `.scad`. **Overruled: build it
in C++ as a node.** There is no auto-include precedent in the repo — MCAD needs
an explicit `use <>` — so a bundled script makes the headline entry point
something the user has to know to include, which is a documentation burden
standing in for a feature. And `$preview` is an ordinary special variable,
reachable from `Parameters` by the same lookup `$fn` already uses, so the preview
passthrough needs no `RenderVariables` plumbing and no script.

The plan's argument for script form was forkability and "a fused node saves
nothing". Both are arguments about the *tool* nodes, which stay exactly as they
are and remain the composable surface — people are expected to call them directly
to build their own sugar, with their own brushes, sizes, and child selectors.

**Do:**

- A `fillet()` node taking `r`, `inner = true`, `outer = true`, and
  `disable_preview = true`. It is `difference(union(child, fillet_tool), round_tool)`,
  calling `buildFilletTool` twice.
- **Preview passthrough, defaulting on, overridable.** With `$preview` true and
  `disable_preview` left at its default, the node returns its child untouched.
  `disable_preview = false` builds the fillets in preview for people iterating on
  them.
- **The four `*_tool` nodes never pass through**, in preview or otherwise. A tool
  is a solid the user is composing with; making it vanish under a render mode
  would break every hand-built sugar.
- **Carry the equivalent SCAD as a comment** above the node, so anyone who wants
  a different composition can read what this one does and write their own:

  ```openscad
  // module fillet(r=2, inner=true, outer=true) {
  //     difference() {
  //         union() {
  //             children(0);
  //             if (inner) fillet_tool(r=r) children();
  //         }
  //         if (outer) round_tool(r=r) children();
  //     }
  // }
  ```

**Acceptance:** `fillet(r=2) cube();` renders filleted under F6 and shows the
bare cube under F5; `fillet(r=2, disable_preview=false) cube();` shows fillets in
both. `inner`/`outer` each suppress their half. A regression `.scad` with a
committed baseline, and a `.csg` dump line for the node.

---

## T2 — `case_two_bosses`, plus a dome variant — **DONE**

Both cases are in `fillet-tests/cases/`, the counts they raise are pinned in
`FilletBuilder_test.cc`, and the suite is at 82 checks all matching. Four things
came out of it, two of which contradict what this section predicted.

- **The curved junction exists and is solved.** Each base ring is cut at both
  crossings into one open arc, so four chains come back — two curved, two
  straight — and both crossings carry three chain ends. The corner solve pins
  exactly one seated ball at each, at every radius probed, nothing is refused or
  run out, and the tool is one connected piece of genus 1: the two arcs close
  into a loop through the two corners. The rings did not survive being cut into
  arcs, and that is the right answer rather than the failure this section was
  watching for.
- **The junction shed a crumb at finer tessellation — FIXED, and it was not the
  corner cell's shape.** At `$fn = 48` the tool came back as three pieces: the
  right one, plus a detached wafer at each junction, 0.076 x 0.061 x 0.005 and
  5.2e-06 of volume, on the plate face where the two ring beads' outer edges
  cross — which is where the corner ball touches the plate. Written up here as
  D2's remainder on a new shape; it was a different defect that happened to
  surface in the same place. A seated ball touches its wall at a point, so the
  overshoot the corner cell stands past that wall had no cutter, and the canals
  running in cut it free. Corner balls now carry a point past each seated wall as
  every arc already did. The case stays at `$fn = 24`, where the question is
  unchanged and a dilation is affordable; the 48-gon reproduction is pinned in
  `FilletCompare_test.cc`.
- **The dome does not run out of constant-radius solutions.** This section
  expected a radius approaching the curvature it follows to have no solution. It
  has one at every size: on a wide enough plate an `R = 8` dome takes `r = 30`
  without a word, correctly, because the crease is a circle in the plate's own
  plane and the seated ball meets the plate one radius outside it. What refuses
  the oversize variant is the **plate's edge** — 12.017 on a plate reaching 20
  from the axis, with the overshoot tracking `r` one for one above that. So the
  dome's curvature is measured to cost the gate nothing, which is the strongest
  statement available that D6 is fixed rather than merely tolerated.
- **Neither case can be given a `sandwich` verdict.** CGAL refuses to dilate
  either applied result, and the controls say it is the kernel: one boss on the
  same plate dilates at `$fn = 24` and refuses at 48 with no junction in it
  either way, two bosses dilate at `$fn = 12`, and radius and clip make no
  difference. The dome refuses down to `$fn = 16`, coarser than the shape is
  about, so there is no version that keeps its question and passes. Both are in
  `expectations.txt` with the numbers.

The rest of this section is what was reasoned out beforehand, kept because it is
the argument for the two shapes that were built.

The one structural unknown left is a **junction where the incident spines are
curves**. Every junction in the suite is a polyhedral vertex where straight
spines meet. `case_pipe_tee_equal` was built to supply one and turned out not to
contain one at all: where the two seam ellipses cross, the cylinders share a
tangent plane, the dihedral falls under the threshold before the crossing, and
the chains come back as four open arcs.

```openscad
module case_model() {
  cube([60, 40, 6]);
  translate([22, 20, 6]) cylinder(r = 10, h = 20, $fn = 48);
  translate([36, 20, 6]) cylinder(r = 10, h = 20, $fn = 48);
}
```

Two base rings crossing at two points, with a straight groove crease running up
between the cylinders: at each crossing, two curved arcs and one straight spine
meeting at a genuine angle. Watch whether the rings survive being cut into arcs,
and what the corner cell does where a curved spine meets a straight one.

**Add a dome-on-plate variant** — the D6 model above — as a second case or a
variant of this one. It pins D6 against regression and it covers the other end of
the same axis: a blend whose radius approaches the curvature it has to follow has
no constant-radius solution, and a dome is the cheapest place to show both the
size that fits and the size that cannot.

---

## D7 — a brush that reaches a junction by less than the setback builds the
## whole chain instead of nothing — **DONE**

Fixed as written, by the second of the two routes: the caller decides. The
mapping into section space still drops a run that collapses to a point, and the
one caller of it now marks the chain unusable when a *non-empty* selection maps
to an empty list — the same `chainUsable` flag a two-station chain truncated
from both ends already sets. The empty-means-whole-chain reading survives
untouched for the chains no brush ever cut. The comment on `toSectionSpace` that
stated the overloaded meaning is gone with it.

The junction was left alone, and that is the answer, not an omission: all three
creases still reach the vertex, so `endAnchored` still builds the corner cell,
and what the brush asked for is exactly the material that cell already carries.

Measured on the table above, `r = 3`, same cube, after the fix:

| `D` | removed |
|---|---|
| 2.0 | 13.399 |
| 2.5 | 13.399 |
| 2.9 | 13.399 |
| 3.0 | 13.399 |
| 3.6 | 16.984 |
| 5.0 | 25.415 |

Monotonic. Below `D = r` it is flat rather than falling, which is right: every
such brush asks for material inside the setback, the corner cell is the whole of
what is there, and the corner is all-or-nothing. (The absolute numbers sit a
little above the ones measured for the report — a different brush box, not a
different tool.)

Pinned by `brush: a selection swallowed by the junction setback builds no bead`
in `FilletBuilder_test.cc`: count, extent, less-than-the-wide-brush, and genus.
`case_brush_halfchain` and the other brush cases are unchanged, the whole
`fillet-tests` suite still matches expectations, and `ctest -R fillet` is green.

**The note below for DOC is now spent** — the one-corner figure may use any box
it likes.

**Since D9, the cube configuration above is decided before this guard is
reached.** A selection that maps to nothing has to lie inside the truncation, and
a chain that does not cover the setback no longer gets a junction to be truncated
by. The guard stays and stays live where the truncation setback exceeds the
radius — a sharp corner, where the seated ball stands further back along each
crease than `r` — but the test's own cube now passes on D9's rule. Read that
test as pinning the surrounding behaviour rather than this guard alone.

**A bug, measured, with a one-line cause and the opposite of the asked-for
behaviour.** Found while writing the doc figures.

Round a single corner of `cube(20)` at `r = 3` with a box reaching `D` past the
vertex, and measure the material removed:

| `D` | removed |
|---|---|
| 2.0 | **113.814** |
| 2.5 | **113.814** |
| 2.9 | **113.814** |
| 3.0 | 13.183 |
| 3.6 | 16.714 |
| 5.0 | 25.003 |

113.814 is all three creases rounded end to end — 3 x 20 x 1.9315 less the corner
— on a model where the brush asked for 2 mm of each. The cliff is exactly at
`D = r`, which is the truncation setback at a 90-degree crease.

**Cause.** `toSectionSpace` drops an interval that maps to nothing:

```cpp
if (mapped.second - mapped.first > 1e-12) out.push_back(mapped);
```

and its own comment two lines above says what an empty result then means:
*"Empty intervals stay empty, meaning the whole chain."* The sentinel is
overloaded. "No selection was given" and "the selection maps to nothing" are the
same value downstream, and they must mean opposite things. A kept interval lying
entirely inside the stretch truncation removes at a junction maps to a single
point, collapses, and the chain reverts to unbrushed.

**This is junction-specific, and the rest of the brush path is correct.** The
same measurement away from a junction is exact all the way down to `r/6` —
a brush `L` long in the middle of an edge removes `1.9315 L` at `L` = 0.5, 1, 2,
3, 6 and 12 mm, within 2%. So a blend shorter than its own radius is buildable,
correct, and worth keeping; only the junction path inverts.

**Do:** distinguish the two meanings. Either carry "selected nothing" as a
separate flag from "no brush", or have the caller decide before calling — the
brush block already knows whether a brush existed. Then a chain whose selection
falls inside the truncated stretch builds **nothing**, which is what was asked
for and is also what `endAnchored` above it already reasons about correctly.

**Acceptance:** the table above becomes monotonic — every `D` below 3 removes
less than `D = 3` does, not nine times more. `case_brush_halfchain` and the
brush cases in `FilletBuilder_test.cc` are unchanged. Add a unit test at
`D < r` on a cube corner; it is a count-and-volume test, not a render.

**Note for whoever writes DOC:** the one-corner example on the doc page uses a
box reaching well past `r` deliberately. Once this is fixed that is no longer
load-bearing, but until then it is the only reason that figure is right.

---

## D8 — the crowding test refuses a third of the radii that fit — **DONE**

Fixed as written: the question was replaced, not the tolerance. The test now asks
whether the other crease's contact point lands in the corner region the blend
occupies — under each wall by no more than the corner reaches, and no further
from the ball centre than the crease itself — instead of whether it lands
anywhere in the seated ball. All three bounds come off `C`, `TA`, `TB` and `v`,
which the contacts already carry, so nothing new is computed or stored.

The one thing the sketch above does not say: **how deep under a wall the corner
reaches is not the radius except at a right angle.** The tool runs out along each
wall to the tangency point, and that point sits `r(1 - cos phi)` under the
*other* wall, `phi` being the angle between the two wall normals — `r` at 90
degrees, `1.5 r` at a 60-degree crease, half of it at a shallow one. Using `r`
flat would have been a new false refusal on shallow creases and a false
acceptance on sharp ones.

The face tolerance now **widens** the region asked about, where it used to
loosen the ball test. That is deliberate and it costs the top 2% of the band:
the last radius accepted on a cube is `r/L = 0.4917`, not 0.5. The doubtful case
there is two beads meeting exactly tangentially, which is the contact that
leaves slivers — refusing it is the right side to fall on.

Measured after the fix, `round_tool` on a cube, all twelve creases:

| cube side | last radius accepted | as `r/L` |
|---|---|---|
| 5 | 2.4167 | 0.4917 |
| 10 | 4.8333 | 0.4917 |
| 20 | 9.6667 | 0.4917 |

`round_tool(r = 2) cube(5)` builds: genus 0, volume **83.9693** at 64 segments —
the same six figures as the independently built hull of eight spheres, whose
symmetric difference with it is 0.013% of the solid. Past the limit every crease
is refused as `Crowded`, and the number reported is now the distance from the
crease to the competing feature rather than from the ball centre to it.

**Nothing bought this by getting looser.** Every `drop` variant is still refused
and still for its stated reason, checked by reading the warnings rather than only
the emptiness: `case_outer_edge_round`/`_bevel` large and `case_hole_mouth_round`
large stay `Crowded`; `case_inner_corner_fillet`/`_chamfer` large, the dome's
oversize variant and the boss 1.5 mm from a plate edge at `r = 2` stay `OffFace`;
that boss still builds at `r = 1.4`. The whole `fillet-tests` suite matches
expectations, `ctest -R fillet` is green, and the existing crowding unit test —
two walls 10 mm apart, fitting at `r = 3` and crowded at `r = 8` — is unchanged,
its limit being 5 either way.

Pinned by `size: two beads sharing a face fit until their tangency lines meet`
in `FilletBuilder_test.cc`, on all three cube sizes, plus the hull comparison.

---

## D8 — the crowding test refuses a third of the radii that fit (as written)

**A false refusal, measured, scale-invariant, and on the most ordinary shape
there is.** `round_tool` on a cube refuses every radius past `r/L = 0.34`, where
the geometry runs out at `r/L = 0.5`:

| cube side | last radius accepted | as `r/L` | geometric limit |
|---|---|---|---|
| 5 | 1.7 | 0.34 | 0.5 |
| 10 | 3.4 | 0.34 | 0.5 |
| 20 | 6.8 | 0.34 | 0.5 |

At `r = 2` on `cube(5)` all twelve creases are refused and the result is the bare
cube, when the answer is an ordinary rounded cube with 1 x 1 mm of flat left on
each face. Built independently as the hull of eight spheres it is a clean solid,
genus 0, volume 83.9693 — there is nothing degenerate about the shape the gate
declines to make.

**Cause: the crowding question is asked of the ball, not of the material the
blend uses.** The test is whether another crease's contact line sits inside this
crease's seated ball. That ball is a full sphere of radius `r` centred `r` off
each wall, so it reaches `r` *along* the face past its own tangency line — into
material the finished blend never touches. The blend occupies only the strip
between the two tangency lines. Two blends on a shared face are compatible while
their tangency lines do not cross, which is `L - 2r >= 0`; the ball test refuses
at `L - 2r < r`, which is `r > L/3`. The measured 0.34 is that third plus the
existing face tolerance.

So the band from `r/L = 1/3` to `1/2` is refused and should not be. A 10 mm cube
cannot be given a 4 mm round today.

**Do not fix this by loosening the tolerance** — the tolerance is calibrated for
tessellation error and would have to grow with `r` to cover this, which is the
same mistake D6 rejected. The question itself is the wrong one. Ask whether the
two tangency lines overlap — the contact points are already computed and are
exactly the right objects — rather than whether a contact line falls inside a
ball whose far half the blend does not use.

**Watch what this must not break.** The gate exists to refuse real collisions and
several cases pin it: `case_round_past_boss` (a boss 1.5 mm from a plate edge,
correctly refused at `r = 2`), `case_near_wall_fillet`, and every `drop` variant.
The obstacle in those is a wall the ball genuinely runs into, not another blend's
tangency line, so a test on tangency-line overlap has to keep answering them —
verify before committing, because "the gate must not buy this by getting looser"
is the whole reason D6 was done the way it was.

**Acceptance:** `round_tool(r = 2) cube(5)` builds, and matches the hull of eight
spheres to the comparison test's tolerance. `r = 2.5` on `cube(5)` is refused, or
builds with zero flat left, but does not silently produce a self-intersection.
Every existing `drop` variant stays refused for its stated reason.

---

## D9 — a corner cell is built at full size however little of it the brush covers — **DONE**

**Re-measured 2026-07-30, and correct, including the excuse.** Built one chain at
a time, each bead spans exactly `[0, D]` along its own crease at `D` = 0.5, 1, 2,
3 and 6, with volume linear in `D` — so the per-spine claim below holds and the
bounding-box confounding it blames is real (any one bead's box reads `r` across
the other two directions). The junction appears at `D = r` and not before. The
volume table below does not reproduce as printed — 24 arc segments give 1.68,
1.70, 13.52 and 31.69 against its 2.06, 2.08, 14.83 and 35.59 — same ratios, same
conclusions, different tessellation, which the table should have recorded. Details
in [`log-2026-07-30-brush-width.md`](log-2026-07-30-brush-width.md).

Done as written, in the one place it belongs: `endAnchored` no longer asks
whether the selection arrives at the end vertex but whether it covers `r` of
crease measured back from it, walked station by station because segments differ
in length. `chainJunctions` calls it with the radius it already has. A chain no
brush touched still anchors on sight, as before.

**What it is worth, measured.** `cube(20)`, `r = 3`, a box reaching `D` past the
top vertex, tool volume:

| `D` | before | after |
|---|---|---|
| 1 | 13.52 | 2.06 |
| 2 | 13.52 | 2.08 |
| 3 | 14.83 | 14.83 |
| 6 | 35.59 | 35.59 |

Below the setback the full corner cell — the seated ball hulled against the
sections the beads stop at, and 13.5 of it whatever was selected — is gone.
At and above the setback nothing changed at all.

**The acceptance table in the sketch below cannot be met as written, and the
measurement it rests on is confounded.** It reads the tool's bounding box, and a
bead's cross-section is `r` across whatever the brush is: three beads meeting at
a corner fill an `r`-sided box around the vertex whether they are 1 mm long or 6.
That is BRUSH-WIDTH's rule — a brush selects, it does not shape — and it is why
the `D = 1` row read 3.003 rather than 1. Per spine, which is the axis the brush
contract is about, the cut is exact: the beads stop where the brush does, at
every `D` measured. What was actually broken is the corner cell, and it is the
volume above that shows it.

**What is deliberately still built at `D < r`: the beads.** Three short beads
meeting at an unclosed corner, mutually cut by each other's canals — 2.06 rather
than the 5.79 three separate 1 mm beads would be. That is the same answer the
operator already gives at any corner it refuses to close, and it is the rule the
existing "a corner one crease is cut short of gets no corner cell" case pins.
Dropping the beads as well — which is what "build nothing at a corner" in the
sketch below asks for — is **BRUSH-WIDTH row 2**, whose whole point is to decide
that by the interval's endpoints and to reuse this rule for the threshold. It
is deliberately not done here: doing it inside `chainJunctions` would put half of
that decision in the wrong file. When it lands, the warning below has to change
with it.

**The warning.** A corner the brush reaches without covering now says so, since
otherwise the user's box is answered with a corner that silently is not there:

```
WARNING: round_tool: the brush reaches the corner at [0, 0, 20] but covers less
         than the radius 3 of the creases meeting there; the beads are built and
         the corner is left open. A corner cell is the full size whatever is
         selected, so it is built only where the brush reaches the radius down
         every edge of it.
```

`uncoveredCorners` is what finds them: three or more ends land on the vertex and
are selected up to it, fewer than three cover the setback.

**Both paragraphs above are superseded by BRUSH-WIDTH, which has landed.** The
beads are dropped with the corner now, `uncoveredCorners` is
`dropUncoveredCorners`, and the warning reads "nothing is built there" and only
fires where nothing at that vertex was covered. The test named in the acceptance
below absorbed D7's and no longer checks beads stopping at the brush below the
threshold, because there are none.

**What this does to D7.** On a cube, D7's collapse is now unreachable from the
brush: a selection that maps to nothing had to lie inside the truncation, and a
chain that does not cover the setback no longer gets a junction to be truncated
by. The guard stays, and stays live, where the truncation setback exceeds the
radius — a sharp corner, where the seated ball stands further back along each
crease than `r`. Its test still passes and still pins the surrounding behaviour,
but it is D9 that decides that case now; the comment there says so.

**Acceptance:** `case_brush_halfchain` and every brush case in
`FilletBuilder_test.cc` are unchanged, the whole `fillet-tests` suite matches
expectations, and `ctest -R fillet` is green. Pinned by `brush: a corner the
brush reaches but does not cover is not built`, which checks the junction count,
the reported corner, the volume either side of the threshold, and — above it —
that the beads stop at the brush and not at the setback.

---

## D9 — a corner cell is built at full size however little of it the brush covers (as written)

**The brush contract is exact along an edge and not at a corner, and the gap is
as large as `r`.** Measured on `cube(20)` at `r = 3`, brushing the top vertex
with a box reaching `D` past it, reading the tool solid's own bounding box:

| `D` | brush covers | tool reaches |
|---|---|---|
| 1 mm | 1 mm down each edge | **3.003 mm** |
| 3 mm | 3 mm | 3.003 mm |
| 6 mm | 6 mm | 6.000 mm |

At `D = 1` the corner cell overshoots the brush by 2 mm, three times what was
selected. Mid-edge the same brush is honoured exactly: the bead is cut square at
the brush wall, which is the whole point of `case_brush_halfchain`. So the
operator promises "cut square at the brush" everywhere except at a junction,
where it silently builds out to the setback.

**It is deliberate, and the reasoning has a range of validity it outgrew.**
`endAnchored` says a junction is built where every chain meeting there *reaches
the vertex*, and its comment argues the overshoot is "the fraction of a segment
truncation needs" and is the lesser wrong against losing the corner outright.
That is true when the brush is comparable to `r`. It is false by a factor of
three at `D = r/3`, and unbounded as `D` shrinks: the corner cell is always the
full seated ball, so the smaller the brush the larger the overshoot in relative
terms.

**A corner cell genuinely cannot be clipped**, which is why the decision is
binary rather than a matter of trimming it. It is hulled from the seated ball and
the sections the beads stop at; there is no perpendicular to cut it against in
three directions at once. So the question is only where the threshold sits, not
whether to build a partial corner.

**Do:** require the selection to cover the stretch the corner cell actually
occupies — the setback, which is the same `D >= r` the beads already effectively
have since D7 — rather than merely to touch the vertex. Below that, no corner.
The contract then reads the same everywhere: nothing is built outside the brush.

Note this makes `D < r` build **nothing** at a corner, where today it builds a
corner with no beads attached. That is the honest answer to "round this corner
with a box smaller than the radius", and it is worth a warning naming the radius,
because it is the one case where a brush the user drew is answered with silence.

**Acceptance:** the table above becomes `tool reaches <= D` in every row. The
figure on the doc page (a 12 mm box at `r = 3`) is unaffected, as is
`case_brush_corner_partial` if it is ever written. `case_brush_halfchain` and the
brush cases in `FilletBuilder_test.cc` are unchanged — none of them brushes a
junction more tightly than the setback.

---

## D10 — a brush thinner than about 0.5 mm loses its cut along the spine — **DONE, and the width was a red herring**

**Re-measured 2026-07-30, and correct.** With `kEdgeSlack` set to zero the test
fails on its first width — zero crossings where it needs one — so the fix is held
rather than merely present. The non-monotonicity is exactly as described below;
the test's own comment implied a threshold and has been reworded. Table in
[`log-2026-07-30-brush-width.md`](log-2026-07-30-brush-width.md).

The section below says to find the tolerance before choosing a fix. **There is no
tolerance, and there is no threshold.** Extending the measurement past the five
widths it was taken at breaks the story immediately — `W` = 0.25 bounded the
crease correctly, 0.3 did not, 0.35 did, 0.4 and 0.45 did not, 0.49 did. Not
monotonic, so not a threshold, and nothing was going to be found by hunting for
the constant. Offsetting the same brush by a hundredth of a millimetre off centre
made every width correct.

**The cause.** A brush drawn symmetric about the crease it selects — the standard
way to name one edge — puts the spine exactly on the diagonal where the two
triangles of the brush's end face meet. Möller–Trumbore then decides the hit on a
barycentric coordinate that is 1 to within an ulp, and **both** triangles can
reject on opposite sides of it. The crossing is lost, `chainSelection` reports the
whole chain as covered, the node's whole-chain rule clears the interval, and the
brush goes on selecting the crease while no longer bounding it. Which way the ulp
falls depends on the magnitudes in the arithmetic, which is why it looked like
width and looked like a threshold.

Two details worth keeping, because they cost an hour to find:

- **The split direction matters.** A face split from `(lo,lo)` to `(hi,hi)` loses
  nothing; the same face split the other way, `(lo,hi)` to `(hi,lo)`, is what
  fails — and that is the one OpenSCAD's `cube()` produces. A reproduction built
  with the first split passes at every width and proves nothing.
- **It is not visible at the operator's own tolerances.** The crossing is not
  merely misplaced, it is absent, so nothing downstream can notice.

**The fix** is one constant in `rayFace`: a barycentric slack of `1e-9`, so a ray
that meets a face on an edge or a vertex is caught by *every* triangle owning it
rather than by none. The pair is one passage in the same direction and the
existing collapse in `segmentCrossings` reduces it to a single crossing — that
code was already written for exactly this shape of event, it was simply never
reached. Seven orders above the noise it absorbs and far below any brush face a
model could mean to place.

**Measured after the fix**, `cube(20)` at `r = 3`, a column of width `W`
straddling one vertical edge and stopping at `z = 14`:

| `W` | 0.02 | 0.2 | 0.3 | 0.4 | 0.45 | 0.5 | 1 | 4 |
|---|---|---|---|---|---|---|---|---|
| bead top | 14.00 | 14.00 | 14.00 | 14.00 | 14.00 | 14.00 | 14.00 | 14.00 |

The selection counts are unchanged at every width (1 of 12 at 0.02, 3 of 12
above it), which is the half that was always right.

**Acceptance:** pinned by `brush: a spine down the middle of a brush face is
still cut by it`, over the same widths, on a hand-built column with the failing
split — the mesh has to be written out in the test, since a manifold primitive
splits the other way and would pin nothing. Verified to fail with the slack
removed. The doc page's single-edge recipe is unaffected, as the section below
predicted.

---

## D10 — a brush thinner than about 0.5 mm loses its cut along the spine (as written)

**A brush narrow enough across the crease stops bounding the crease along it.**
Measured on `cube(20)`, a column of width `W` straddling one vertical edge and
stopping at `z = 14` on an edge that runs 0 to 20:

| `W` | selected | bead top |
|---|---|---|
| 0.02 | 1 of 12 | **20.00** — runs the whole edge |
| 0.2 | 3 of 12 | **20.00** — runs the whole edge |
| 0.5 | 3 of 12 | 14.00 — correct |
| 1 | 3 of 12 | 14.00 — correct |
| 4 | 3 of 12 | 14.00 — correct |

The selection is right in every row — the brush restricts *which* creases are
taken, at every width. What is lost below the threshold is the **cut along the
spine**: the interval's far end stops being honoured and the bead is built to the
end of the chain.

**It does not scale with the size.** Identical rows at `r = 1`, `r = 3` and
`r = 6`: the cutoff sits between `W = 0.2` and `W = 0.5` in all three. So this is
a tolerance somewhere in the brush's own geometry — read off the mesh or the
model's extent — and not one of the size-derived constants. Whoever picks this up
should find what that tolerance is before choosing a fix; the measurement above
says where to look but not which constant it is.

**Why it has not bitten:** every brush in the suite and in the regression models
is millimetres wide. The one place it is reachable is the single-edge recipe on
the doc page, which uses a 0.02 mm column — and there the brush runs past both
end faces anyway, so the interval is the whole chain either way and the picture
is the same. That recipe therefore survives a fix; it is not resting on this.

**Acceptance:** the table above reads 14.00 at every width down to the point
where the brush no longer contains the spine at all. Below that it should select
nothing rather than select-and-not-bound.

---

## BRUSH-WIDTH — pin what a narrow brush does, and say it on purpose — **DONE**

Done, and two things about it are not as the sketch below expects. Both are in
[`log-2026-07-30-brush-width.md`](log-2026-07-30-brush-width.md) with the
measurements; in short:

**Row 2 of the table keeps the debounce.** The sketch argues that a chain-ended
interval cannot be the tangency artefact and so needs no length test. `brush: a
graze too short to be a bead is dropped` is a counterexample already in the tree:
its 0.001 mm slab straddles the crease's start vertex, so the stub it leaves is
chain-ended at one end and is still the artefact. One cut end is enough to ask
the question. The exemption is therefore scoped to a stretch the brush cut at
*neither* end — the whole crease, selected entire — which is the row the reported
bug is in, and the L reproduction below now builds the same tool as the unbrushed
call, byte for byte.

**Row 2's corner rule is in, and its warning had to be scoped.**
`dropUncoveredCorners` (replacing `uncoveredCorners`) drops every stretch that
reaches a corner without covering `r` of crease back from it, so `D < r` at a
corner now builds nothing rather than three stubs — which is what D9 left to this
item. But warning at every such corner would fire on the single-edge recipe,
whose four neighbour stubs are dropped by exactly this rule: precisely the case
this item's own text says not to warn about. So the warning is scoped to a corner
where *nothing* was covered — a brush drawn around a corner and answered with
silence — and a corner some crease through it does cover is silent, with the echo
line's edge count reporting the drop.

That scoping is what makes the recipe writable with a brush a model can draw:
1 of 12 at every width up to just under `2 r` across, 5 of 12 above it, the limit
being how far the brush reaches *from* the edge. The doc page no longer needs the
0.02 mm column and no longer explains the recipe by the debounce, which had
become the wrong reason.

`minLength` is `debounce` now, at both ends, and the comment at the constant says
nothing rests on its value — the sketch's item 3 asked for the opposite, because
when it was written the constant was what made the recipe work.

The three "Do" items below are done as written except item 0, which is the row
above, and item 3, which inverted with it. The sketch follows unchanged.

---

## BRUSH-WIDTH — pin what a narrow brush does, and say it on purpose (as written)

Found while writing the doc figures, measured, and currently correct but
unpinned and unstated.

**A brush selects; it does not shape.** The blend built along a selected stretch
is the full requested profile however narrow the brush is. Measured on a 20 mm
cube edge at `r = 3`: a brush 0.02 mm wide across the edge and a brush 8 mm wide
give the same **2.32670** of tool volume per millimetre of edge — identical to
six figures. The brush clips the spine, not the section.

**That makes "one whole edge, and only that edge" expressible, via a rule written
for something else.** Any brush tall enough to contain a full vertical edge of a
cube also contains the first few millimetres of the four horizontal edges meeting
it, so the obvious brush selects 5 of 12, not 1. Making the brush narrow enough
puts those four overlaps under `minLength = 0.01 * size` and they are dropped, so
the count is 1 of 12 and the one edge is blended over its full height.

The comment at that constant says the stub it drops "is never intentional" — it
was written for a brush face nearly tangent to the spine crossing it twice a hair
apart. The doc page now leans on it deliberately, which is a second job the
constant was not given on purpose.

**No warning or log accompanies any of this**, which is right: the echo line
already reports `takes 1 of 12 candidate edge(s)`, and the count is after the
drop, so the user sees the answer without a warning about a stub they never
asked for. Do not add one — a warning would fire on the accidental case the rule
was written for, which is exactly the case nobody needs told about.

### The threshold is one constant answering two different questions — split it

The obvious framing is "pick a value for `minLength`", and it looks like a trade:
raise it and the recipes become writable, but deliberate short work disappears.
**That trade is an artefact of applying one constant everywhere, and it goes away
if the rule is split by position instead.** Settle this before writing the tests
below, since they pin whichever rule is chosen.

**Formulate it on the interval's endpoints, not on its length.** The question at
each end of a kept interval is *what put it there* — the brush cut the chain, or
the chain simply ends. Three cases, and only one of them has a length condition:

| ends the brush cut | what the interval is | rule |
|---|---|---|
| neither | the brush covers the whole crease | build it, at any length |
| one | the brush runs off one end of the crease | length matters only for the corner at the covered end |
| both | a window strictly inside the crease | debounce only |

The first row is the case a length test gets wrong: a crease **shorter than the
setback, selected whole**. Nothing is being clipped there — the crease is simply
that short — so a rule phrased as "cover the setback" would refuse a selection
that asked for everything there is. `chainSelection` already distinguishes the
two kinds of end: an interval starting at `0.0` or ending at `segments` is
chain-ended, anything else is a brush crossing.

**"At any length" has to mean no length test at all, debounce included.** The
debounce exists for a brush face grazing the spine and crossing it twice a hair
apart — an artefact that by definition has *two brush-cut ends*. A chain-ended
interval cannot be that artefact, so applying the debounce to one is a category
error, and it is not a harmless one: `minLength` scales with the size, so at
`r = 30` it is 0.3 mm in absolute terms and a legitimately whole-covered chain
shorter than that would vanish. Scope the debounce to the third row only.

Note what that also fixes: `chainSelection` runs **only when a brush is present**,
so today a chain shorter than `0.01 * size` builds with no brush and disappears
the moment any brush is added, including one that covers the entire model. A
brush covering everything must be a no-op, and today that is true only because
the failing case is hard to reach.

**Reachable, and here is the reproduction.** A long thin L decouples the crease's
length from everything the gate measures: the arms give the blend all the room it
needs along both walls, while the crease itself is only as long as the plate is
thick.

```openscad
module part() { cube([1000, 5, 2]); cube([5, 1000, 2]); }

fillet_tool(r = 400) part();                       // builds, genus 0

fillet_tool(r = 400) {                             // builds NOTHING
    part();
    translate([-9000, -9000, -9000]) cube(18000);  // a brush containing the model
}
```

The one concave crease is 2 long; `minLength` is `0.01 * 400` = 4. Both
invocations classify identically — `selects 1 concave edge(s)` — and the second
then reports:

```
WARNING: fillet_tool: the selection brush covers none of the 1 candidate edge(s);
         nothing is built. The brush has to contain part of an edge, not merely
         touch the model.
```

which is false twice over: the brush contains the whole model, and it covers the
whole edge. **A brush that contains everything is not a no-op**, and the message
blames the user for the opposite of what they did.

Note what makes it reachable — a large `r` with a short crease, which needs the
crease's length to be independent of the room the blend has. On a boss or a plate
those are the same number and the size gate refuses first, which is why an
earlier probe across twelve rounded shapes found nothing. On an L they are the
arm length and the plate thickness, and nothing couples them.

**Acceptance:** the second invocation above builds the same tool as the first.
Add it as a builder test — it is two cubes and a count, no render.

**Measured, that first row appears to be unreachable today, and for a good
reason.** A plate with a square boss at `r = 3`, where each base crease is `W`
long:

| `W` | result |
|---|---|
| 12 mm | builds, blend volume 72.342 |
| 6 mm | builds, 38.850 |
| 4 mm | refused by the size gate |
| 2 mm | refused by the size gate |

and the refusal is correct — a crease `W` long puts its two end junctions `W`
apart, so the creases meeting there are separated by `W`, and `W < r` means two
blends genuinely competing for the same material. Short crease and crowded
neighbours are the same condition seen twice. So the first row costs nothing to
support and should still be written that way: it is the honest statement, it is
free, and the day a shape reaches it the alternative would be a silent refusal of
a selection that asked for everything available.

The remaining two rows are the populations that do overlap in practice:

- **An interval that reaches a chain end.** Its end is a junction, and the
  material there belongs to the corner cell. This is the one D9 is about, and it
  wants the setback: cover less than that and neither a bead nor a corner is
  honest.
- **An interval strictly inside a chain.** Nothing about a junction applies. The
  only reason to drop one is the artefact the constant was written for — a brush
  face nearly tangent to the spine, crossing it twice a hair apart — and that
  wants a debounce, not a size.

Measured on `cube(20)` at `r = 3`, mid-edge intervals are exact at every length
tried: `L` = 0.5, 1, 2, 3, 6 and 12 mm remove `1.9315 L` to within 2%. There is
nothing wrong with a 2 mm round of 3 mm radius; it is mostly end caps, and it is
what was asked for.

**And the two populations line up exactly with what each recipe needs.** In the
single-edge brush, the four neighbour stubs to be dropped all begin *at the
shared vertex* — they are end-touching by construction, because the brush
straddles the edge those four meet. The window to be kept is mid-chain. So:

| rule | single-edge recipe | mid-edge window | whole short crease |
|---|---|---|---|
| `0.01 r` everywhere (today) | needs a 0.02 mm column | works | works |
| `1.00 r` everywhere | 2.9 mm slab — writable | **lost** | **lost** |
| by endpoint, as above | **2.9 mm slab — writable** | **works** | **works** |

The third row is strictly better than either uniform value, and it is the same
rule D9 is already putting at corners rather than a second one to explain. The
whole brush contract then reads: *a selection is honoured where it lies, except
that reaching a junction means covering the setback there.*

`endAnchored` already asks precisely "does this interval reach the end", so the
test that distinguishes the two populations exists and is in the same file.

**Why after D9.** D9 fixes the end case at corners. Doing this one first would
mean choosing a threshold that D9 then has to agree with; doing it after means
reusing D9's, whatever it is. The two must not disagree — that is the failure
this whole item exists to prevent.

**D9 is in, and here is the threshold to reuse.** It is
`endAnchored(m, chain, front, r)` in `FilletBuilder.cc` — does this chain's
selection cover `r` of crease measured back from that end vertex, walked station
by station. D9 uses it in one place only, `chainJunctions`, to decide whether a
corner cell is built; the *bead* on such an end is still built, which is row 2 of
the table above and is left to this item deliberately. `endTouched` next to it is
the old arrives-at-the-vertex test, which is what row 2 needs to tell "the brush
cut this end" from "the chain ends here". Row 1 already falls out of it: the walk
stops at the far end of the chain, so a crease shorter than `r` and selected
whole is covered by definition and anchors. The one thing to carry when it lands
is the warning D9 added at an uncovered corner — it says the beads are built
there, and would have to say something else.

**Do:**

0. Settle `minLength` per the above, once D9 is in. Whatever is chosen, record
   the number and the reason at the constant, because the next person will read
   `0.01` and assume it is a debounce.

1. A unit test in `FilletBuilder_test.cc` pinning the section against brush
   width — two brushes of very different widths over the same stretch of one
   crease, same tool volume per unit length. This is the property the doc page
   states and nothing currently holds it.
2. A second one pinning the narrow-brush selection count at 1 of 12 where a wide
   one gives 5, so the documented recipe fails loudly if `minLength` is ever
   retuned.
3. Extend the comment at `minLength` to say it has a second, deliberate use: it
   is what makes a full-length single-edge selection reachable, and it is
   documented as such. Someone tightening that constant to chase a tangency
   artifact would otherwise break a documented recipe with no test in the way.

Note the two tests are cheap — they are builder-level, not renders.

---

## D11 — a spine that turns from one wall onto the next — **DONE, and it was not the crease the warning named**

**A false refusal at float-noise magnitude, on the most ordinary shape the
feature has.** An L bracket — two slabs — warns:

```
WARNING: fillet: radius 2 does not fit the crease at [0, 10, 12] - the blend
         would leave the surface it is meant to meet, by 4.44089e-16.
```

Measured: it fires on every straight L tried — `30x10x26 + 30x26x12`,
`20x6x26 + 20x26x6` — at `r = 1` and `r = 2` alike, so it is not radius
dependent.

**What the sketch got wrong.** `[0, 10, 12]` is where the reflex crease meets the
open end face, so it reads as that crease's endpoint being refused. It is not.
The reflex crease passes the gate, the inner bead is built, and the warning
belongs to the **convex** half: a three-vertex chain `(0,10,26) - (0,10,12) -
(0,26,12)` that runs the end face's own outline and turns 90 degrees at the L's
reentrant corner, from the wall above it onto the wall beside it. What
`fillet(r) l_bracket()` was losing is the rounding of one end face's outline, not
the inner blend.

**Why the turn is refused.** The station at a turn averages the normals of two
different walls, so the ball seated there touches, on either wall, the crease
*between* them — on the boundary of each, by construction, exactly, and at any
radius. That is the off-face test's pass/fail line met exactly, and there was
nothing left for the last bit of a double to decide. The end-of-chain exemption
never applied because the turn is an interior station.

**The fix.** A turn is exempt from the touching question on the side that turns,
for the same reason the chain's ends and its junctions are: the ball rolling from
one wall onto the next is what a chain that turns *is*, and it says nothing about
the size. `chainContacts` compares the sided surfaces of the edge arriving at a
station against those of the edge leaving it, and skips the question on a side
where they differ. Nothing else moved: no tolerance was widened, and the samples
either side of the turn each ask about one wall, so a size that genuinely does
not fit is still refused there — an L whose upper slab leaves only 1 mm of wall
still drops its `r = 2` outline round.

**Landed:** both proportions above at `r = 1` and `r = 2` build with no warning,
inner and outer; the reflex crease reads `Fits`. `FilletBuilder_test.cc` pins the
four combinations and the thin-wall refusal beside them. Whole unit suite and all
82 case checks unchanged.

### The same 4.4e-16 again, on two bosses, and the exemption could not reach it

**Also landed, and it was costing a whole rim.** Two overlapping bosses warned
once at `r = 2` — `the blend would leave the surface it is meant to meet, by
4.44089e-16` — and dropped the convex chain running the merged top face's
outline, so one boss's top rim came back sharp: 42 vertices of it left on the
un-rounded rim, and the warning blamed a radius that fits with 8 mm to spare.

It is the same pass/fail line met exactly, in a place no exemption reaches. The
outline the round tool takes there *is* the boundary of the top face it is asked
about, so the ball's nearest point on that face and the nearest point on its rim
are the same point computed two ways, and which comes out larger is the last bits
of a double. The turn exemption does not apply — nothing turns — and neither does
the end-of-chain one, since the chain is closed.

So the off-face test now reports a miss only above `1e-9` of the size, which the
crowding half of the gate has always had in `faceTol` and this half never did.
Six orders above the noise and far below anything a mesh could mean: a size that
genuinely does not fit misses by a fraction of itself, and the L bracket's
`35 mm` bead on a `30 mm` face still misses by exactly 5. Two bosses now build
with no warning and no sharp rim, 31913.7817 against 31988.3324 — the 74.55 mm3
is the rim that was never being rounded. Every other model in the set is
unchanged to the digit, and the whole unit suite, the 21 baselines and the 82
case checks are unchanged.

---

## D12 — a bead that ends on an outer face leaves a sharp lip over the round

**Where the inner blend meets the outer one, the outer one does not know the
inner one happened.** `fillet()` builds both tools from the *same* original child:

```
difference() { union() { child; fillet_tool(child); } round_tool(child); }
```

So the round tool is seated against walls that the fillet tool has already
changed. Where a concave bead **runs out to an open face** — every extruded L, T
or rib profile, which is to say the shape most people will try — the bead's end
cross-section is left standing on that face as a sharp-edged crescent, overhanging
the rounded outline beside it. Visible in a close-up of the L bracket at
`r = 2`: a lip of `6.568` mm³ across the two ends.

Closed beads do not show it. A boss ring, a bore ring, a rib's base ring never
terminate on a face, so there is no end cap to leave behind, and the two
compositions below agree exactly on all of them.

**The obvious composition fixes it and cannot be adopted.** Running the round tool
on the already-unioned solid rounds the bead ends over into a continuous
transition, and on polyhedral models it is strictly better — on the L bracket the
round pass then reads 35 convex creases and **no** spurious concave ones, against
19 and 1 before; on the boss it *reduces* spurious concave features from 32 to 1.
But on anything whose blend surface is curved it is destroyed, measured at
`r = 2`, `$fn = 32`:

| model | as built now | round tool on the filleted solid |
|---|---|---|
| cube | genus 0 | identical — no concave creases, so nothing changes |
| boss on plate, blind bore | genus 0 | identical result, fewer spurious features |
| L bracket | genus 0, the lip | genus 0, **lip gone** |
| rib on plate | genus 0 | genus 0, a wash |
| pipe tee | genus 0, no warnings | **genus 31, 21 pieces, 31 warnings, 2963 mm³ gone** |
| two bosses | genus 0, no warnings | **genus 1, 33 warnings** |
| dome on plate | genus 0, no warnings | **11 warnings, beads dropped** |

**`min_angle` does not separate the two.** Swept 18 / 30 / 45 / 60 on the three
curved cases: the tee stays genus 12–31 with 31+ warnings throughout. The creases
being wrongly selected are not low-angle slivers, so no threshold reaches them —
they are the bead's own surface, at honest angles, being read as a wall that wants
rounding. This is *not* the tangential-contact sliver problem the *Re-filleting*
caveat describes; it is a second, larger one hiding behind it.

**So the fix has to be selection, not composition.** The round pass needs to
distinguish a convex crease that bounds the original solid from one the fillet
pass introduced, and round only the first kind plus the bead **end caps**. Two
routes worth measuring:

- Classify convex creases on the original child as today, but seat the tool's
  contacts against the filleted solid, so the geometry sees the wall that is
  really there.
- Classify on the filleted solid and subtract every crease lying on a surface the
  fillet pass created, keeping those where a bead is truncated by an original
  face — which is exactly the end caps.

**Either route costs `fillet()` its "it is only sugar" framing, and that is the
part to decide first.** See the note under DOC below. The composition is quoted as
user-writable SCAD in three documents and in `FilletNode.cc`, and *running* one
tool on the other's output stays writable — a module with the children forwarded
into it does that. What is not writable is either route's actual rule, because
both need to know which surface came from which pass, and no `.scad` can ask that.

**Acceptance:** the L bracket's bead ends round over into the outline, with no
lip; every curved case above stays at its current genus and warning count; and
whatever `fillet()` is documented to equal is something a reader can actually
write, or is documented as no longer being sugar.

### The provenance route was built, and is rejected — do not rebuild it

The second route above was implemented end to end and it works: the round pass
runs on the blended solid, `fillet()` stamps the blend with a source id on the
way in, and the round pass drops a convex crease with a wholly-stamped surface on
both sides. Measured, it gave the whole table — L bracket lip gone at 6.568 mm3,
every curved case bit-identical to today, zero warnings, all 82 case checks and
all 21 regression baselines unchanged.

**It is still the wrong answer, and the reason is an invariant, not a
measurement.** `round_tool` must return the same solid for a given input however
that input was made — hand-authored, imported, or handed over by a fillet pass.
Threading "these surfaces are mine" from `fillet()` into the builder couples two
tools through the wrapper and makes the wrapper privileged: it can then do
something no caller composing the four tool nodes can reproduce, and the tools
being the composable surface is worth more than this bug. The commit that did it
is reverted. If it is ever proposed again, this paragraph is the answer.

### What the composition change costs on its own — measured

`fillet()` running its round pass on the blended solid, with no other change, is
the naive composition, and it is what the module in DOC below writes. At
`r = 2, `$fn = 32`: cube, boss, two bosses and dome unchanged; L bracket fixed,
13292.8310 -> 13286.2630; **pipe tee destroyed**, genus 5, 25 warnings, 5633 mm3
gone; rib 6 warnings. Severity moves with tessellation, so anything proposed here
has to be measured at several: at `$fn = 48` the tee survives.

### Where the spurious creases come from — located, and it is not the classifier

Measured on the blended tee at `r = 2, $fn = 32`, splitting every convex feature
edge by whether the blend made both of its faces:

| | count | dihedral |
|---|---|---|
| both faces the model's | 96 | all exactly 90 deg |
| both faces the blend's | 216 | 17.2 .. 175.8, **196 of them above 150** |
| one of each | 0 | — |

So the noise is not a rim of slivers between the bead and the wall, and it is not
low-angle: it is almost all **folds**, faces turning back on each other, and it
lies wholly inside the blend. That is why sweeping `min_angle` cannot reach it,
and it also rules out the "group the bead's facets into one surface by tangency
continuity" idea in the form it was proposed: facets 157 degrees apart are not
tangent to each other and no grouping rule based on smoothness will join them.

**The tool's own mesh already carries them**, which is where this stops being a
classifier question:

| | convex feature edges | of those, above 150 deg |
|---|---|---|
| tee, model alone | 96 | 0 |
| tee, `fillet_tool` alone | 328 | 260 |
| tee, model + tool | 312 | 196 |
| boss, `fillet_tool` alone | 192 | 128 |
| boss, model + tool | 76 | **0** |

The boss absorbs every one of its tool's folds and the tee absorbs almost none.
Located on the boss, the folds sit at `z = 6 - 0.002` and `rad = 10 - 0.002` —
one `eps` behind each wall, along the whole tangency line. They are the overshoot
ladder D2 built: the tool stands `eps` past each wall so the caller's boolean
cuts across a face instead of arriving along it, and a thin ledge is what that
overshoot looks like from outside before the union swallows it.

**The union swallows it only when the wall is flat.** `eps` is `1e-3 * r` and it
is stepped from a point *on the mesh* — a vertex of the wall. On a curved wall
the facets either side of that vertex fall inside it by their own sagitta, which
at `$fn = 32` on a radius-10 pipe is **0.048**, twenty-four times `eps`. So the
step clears the wall at the station it was measured at and stands proud of it in
between, and the ledge survives into the finished solid with nothing but the
tool's own faces on either side of it. Nothing downstream can tell that from a
crease of the shape.

### The fix this points at — landed

Floor the overshoot at the wall's own sagitta, measured off the target's mesh
rather than derived from the caller's facet angle — the same argument `ballPast`
already makes for the ball's own facets, and the same "fix the mesh at the
source" that closed D2. Probed with a crude global floor (the coarsest sub-right-
angle seam in the whole mesh), the result is exact:

| | convex feature edges on model + tool |
|---|---|
| tee, `eps` as now | 312, of which 196 are folds |
| tee, floored at the mesh's sagitta | **96 — the model's own, and nothing else** |
| boss, floored | **76 — likewise** |

That is the whole of the noise, gone at the source, with no provenance, no new
parameter and no change to the classifier. It should improve re-filleting for the
same reason, which is the caveat under DOC.

**How to measure it locally was the open question, and it is now closed.** Two
earlier estimators did not converge, and both were measuring the wrong thing:

- `0.5 * L * tan(turn / 4)` over the seams of the walls the crease runs between,
  which reads a long nearly-flat triangulation diagonal as a chord of an enormous
  circle: 0.737 on an L bracket, 37 % of `r`, where the true answer is zero.
- The station's height above its own neighbouring facet planes, which was under-
  reaching badly: on a pipe the facets carrying the crease are chords 60 mm long
  with no vertex anywhere near it, so there is nothing at the station to measure.

**Landed:** the overshoot at one station of one wall is the fixed hair plus how
far that wall has fallen behind the tangency plane *at the tangency point*, which
is the far edge of the footprint and so the deepest the wall gets under the tool.
The wall is walked from the triangle the station names, out to the tangency point
and no further, stopping at the first crease — `isFeatureAngle`, which is why the
four builders now take the threshold. The quantity is the curvature of the wall
across the setback, `t^2 / 2R`, not the tessellation sagitta: 0.2 on a radius-10
pipe at a 2 mm setback, a hundred times the hair, and the tessellation figure
`0.048` that the crude floor suggested is four times too small.

| at `r = 2`, `$fn = 32` | convex feature edges on model + tool | of those, folds |
|---|---|---|
| tee, `eps` as it was | 338 | 204 |
| tee, overshoot measured | **132 — all square, the model's own rims** | **0** |
| boss, `eps` as it was | 46 | 2 |
| boss, measured | 44 — the model's own | 0 |

Flat walls are untouched, since nothing dips on one: the whole unit suite, all 21
regression baselines and all 82 case checks are unchanged, `FilletCompare_test.cc`
included — the deeper burial is on curved walls, where those comparisons have no
hand reference. Cost is nil: a boss at `$fn = 128` renders in 633 ms against 628,
the tee in 95 against 99.

Two things that came with it. The corner cells' `over` and the corner ball's
`ballPast` are no longer `1.5 x` and `2 x` one number for the whole tool but that
much past the deepest bead arriving at each junction, because the ladder is only
meaningful if the cell stands further past a wall than the beads it closes. And
`RoundSection` carries the overshoot it was built at, which is what a truncated
end hands its corner.

**What this does not fix is the composition, and the composition is now refused
for a different reason than it was.** With the noise gone, route A — the round
pass on the blended solid — no longer tears curved work apart. Measured through
the node itself at `r = 2`, `$fn = 32`, against the composition as it ships:

| model | as it ships | round pass on the blend |
|---|---|---|
| cube, boss, blind bore, dome | genus 0, no warnings | identical, to the digit |
| L bracket | 13292.8286 | **13286.2607 — the lip, 6.568, gone** |
| pipe tee | 20354.7878, no warnings | same volume, genus 0, one piece — **8 warnings** |
| two bosses | 31988.3324, 1 warning | same volume — **32 warnings** |
| rib on plate | 17557.3154, no warnings | **17674.7152, 6 warnings** |

**The rib is what kills it, and it is not the warnings.** That +117 mm3 is the
rib's four vertical rounds and its top rim not being built at all: the refusals
are at `[15, 17, 26]` and `[15, 23, 26]`, which are the rib's own corners, and the
solid comes back with a vertex sitting on the sharp corner line and four vertices
on the top face where the unblended build has twenty-eight. The gate reads
"another feature 4.33 away needs the same material" because on the blended solid
the competitor it finds is the base bead's own flank.

So route A needs the size gate to be right about a solid a bead is already in,
which is the second half of what the provenance commit had to do and is exactly
the coupling that commit was reverted for. Nothing here is a warning-tuning
problem: suppressing those six lines would leave the rib silently unrounded.
The lip is still open.

### What the rib says, whichever route is taken

Running the round pass on the blend changes the rib's answer by 6.9403 mm3 and it
is not a defect: the rib's four vertical creases stop where the base bead takes
over, so their rounds stop there too instead of running past the end of their own
creases and notching the bead. Pictures either side of that settled it. Whatever
lands has to expect the rib to move.

---

## DOC — user-facing documentation

The feature has zero mentions outside `src/`, `tests/` and the design directory.
Needed:

- An entry in `doc/openscad.pyi` for `fillet()` and the four tools.
- One example under `examples/`.
- A `RELEASE_NOTES.md` line.
- Three caveats documented explicitly, because each one is a support question
  waiting to happen:
  1. **`min_angle=`** — what the derived threshold is (`1.5 x` the caller's facet
     angle), and that this is the escape hatch when it picks the wrong edges.
  2. **Preview default** — `fillet()` shows the base model only under F5. Note
     that a fillet adding material into a clearance gap will not show its
     interference in preview.
  3. **Re-filleting** — there is deliberately no re-fillet rule (see Cancelled);
     the angle threshold is the only protection and `min_angle=` is the override.
     Note what T1 found: an already-filleted mesh classifies with spurious
     creases of both signs, because the bead's tangential contact triangulates
     into slivers. Filleting a fillet is therefore not reliable today, and the
     honest doc line says so rather than implying the threshold handles it. If
     D2 lands, revisit this line.

### The quoted SCAD equivalent — what can and cannot be written

`fillet()` is documented as sugar for a composition a reader could have written,
and that claim appears in four places: the comment above `builtin_fillet` in
`FilletNode.cc`, `doc-page/fillet.md`, the wiki draft, and `pr-body/pr.md`. All
four quote the child **twice** — once inside the union, once as the round tool's
argument — which is fine only because both quotes are of the *original* child.

The moment either tool has to run on the *result* of the other (D12), the
equivalent has to refer to an intermediate solid twice. That **is** writable: put
it in a module and forward the children into it.

```openscad
module my_fillet(r = 2, inner = true, outer = true, min_angle = undef) {
    module blended() {
        union() {
            children();
            if (inner) fillet_tool(r = r, min_angle = min_angle) children();
        }
    }
    difference() {
        blended() children();
        if (outer) round_tool(r = r, min_angle = min_angle) blended() children();
    }
}
```

Verified exact against the C++ composition it mirrors — same triangle count, same
`13286.2607` mm³ — and the full signature behaves: `inner = false`,
`outer = false` and `min_angle = 40` each give the solid they should.

The `blended() children()` at both call sites is the whole trick and is not
optional. `children()` **inside** `blended()` means *`blended()`'s* children, so
writing the calls as a bare `blended()` and relying on the definition to see the
outer children produces **nothing at all, silently** — both branches come out
empty, with no error and no warning. The `.csg` dump is where it shows: every
`children()` becomes a bare `group()`. It is the first thing a reader will try, so
it is worth a sentence in the docs.

What is *not* writable is **sharing** the result — but that costs far less than it
looks, and it is worth knowing exactly what gets repeated, because the answer is
"the cheap half".

`smartCacheInsert` runs from `collectChildren*`, which means a node's geometry
enters the cache at its **parent's** postfix, once every sibling has been
traversed. So:

- The `fillet_tool` inside the first `blended()` is inserted when that `union`
  collects its children, which happens *before* the second `blended()` is
  traversed. The second one **hits the cache**. The tool is built once.
- The two `union` nodes are siblings under the `difference`, so they are inserted
  only at the *difference's* postfix, after both have run. Neither can see the
  other. The union boolean is the one thing genuinely repeated.

And the repeated boolean is the cheap half. Broken down on a boss at `$fn = 128`:

| | ms |
|---|---|
| the boss alone | 56 |
| `fillet_tool` on it | 314 |
| `union` of the two | 332 — so **the union boolean is ~18 ms** |
| `round_tool` on the plain boss | 488 |
| `round_tool` on the filleted boss | 767 — the round pass costs the same either way |

So the whole composition is two ~300–430 ms tool passes and one 18 ms boolean, and
the 18 ms boolean is the only thing a duplicate reference repeats. The forwarded
form measures 780 ms against 744 for the current composition, and almost all of
that 36 ms is composition B doing a second `union`, not the SCAD being written
badly.

**A duplicated sibling can be made to dedupe, by adding one level above the first
occurrence.** A node's geometry is inserted at its *parent's* postfix, so wrapping
the first occurrence in a `union()` or `group()` gives it a parent that finishes —
and inserts — before the second occurrence is traversed. Measured on a
deliberately expensive union of two `$fn = 200` spheres:

| | ms |
|---|---|
| the union once | 204 |
| the same union twice, as siblings of a `difference` | 322 |
| twice, with an extra `union()` around the first | **214** |
| twice, with an extra `group()` around the first | 215 |

It recovers essentially all of it, and it is worth knowing as a general OpenSCAD
technique. It does **not** help here, for the reason the table above gives: the
duplicated union is 18 ms out of 780, so there is nothing to win — measured 781 ms
with the wrapper against 780 without, which is noise. Reach for it when the shared
subtree is expensive, not by reflex.

Two more notes. Duplicated top-level siblings otherwise never dedupe, and this is
not specific to the fillet nodes — two identical `fillet_tool` calls side by side
cost what two different ones cost (364 ms vs 361 ms, against 210 ms for one), and
so do two identical plain unions (322 vs 327, against 204). And whether the
intermediate is written as a nested module or a repeated inline expression makes
**no difference to any of it**: instantiation is inlined into the node tree, so both
give the same tree bar `group()` wrappers, the same cache keys, and the same time
to the millisecond (88 ms each). The submodule buys readability, nothing else.

So whichever route D12 takes, decide deliberately which of these the docs say:

1. **Quote the forwarded form above.** Honest, runnable, and legible — the
   duplicated-expression objection is gone, and the cost is one repeated boolean
   rather than a repeated blend.
2. **Quote it and note the node computes the shared solid once.** Same snippet,
   plus one sentence; the equivalence becomes "up to sharing". Preferred: the gap
   is an implementation detail, not a difference in result, and it is small.
3. **Stop calling it sugar.** Required outright if D12 lands as a selection change
   rather than a composition change, because "round only the creases the other
   pass did not create" is not sayable in `.scad` at any length.

Whatever is chosen, all four copies move together.

---

## CLEAN — before merge

- Delete `fillet-feature-design/`. Its own convention calls it scaffolding, and
  the source deliberately carries no references to it.
- Move `fillet-tests/` under `tests/` or delete it. It is the place a case is
  prototyped before it has a baseline; decide whether that role survives the
  merge.

---

## Cancelled — do not re-open these

Each of these appears as future work somewhere in
[`detailed-milestones.md`](detailed-milestones.md) or
[`fillet-operator-plan.md`](fillet-operator-plan.md). They are decided, not
pending.

- **M11, re-fillet tagging** (reserved ID ranges, `ReserveIDs`, the MeshGL
  `runOriginalID` stamp). **Cancelled.** A single angle-based check is the
  preferred protection, and it already ships with a user override in
  `min_angle=`. Re-filleting is the user's business; the requirement is that it
  does not crash, which is the same requirement every other operator carries and
  which T1 pins. The tagging machinery was the riskiest step in the plan and buys
  a rule nobody asked for.
- **Section 11 analytic fast paths** (exact torus for plane-meets-cylinder, exact
  prism-minus-cylinder). **Cancelled.** The plan itself already demoted these to
  "optional speed optimisation, to be taken only if profiling demands it" when it
  rejected node-tree pattern-matching in section 10. Nothing has demanded it.
- **The circular-segment section** (written at M7 and backed out). **Cancelled as
  a work item.** It was two things: a vertex-count saving nobody asked for, and a
  possible route to D2. D2 landed without it, so neither reason survives.
- **A second classifier filter on face area, edge length or triangle quality**,
  beside the angle test. **Cancelled — measured, and the two populations are
  inverted.** The numbers are under D2. It is not a matter of tuning the constant
  or exposing it as a parameter: a real crease sitting against a bead has worse
  triangles than the bead's own noise, so every threshold takes the feature
  first.
- **A "fills" check for the test harness** — no point of the model within the
  tool's reach of a selected crease may be left unblended. **Cancelled.** There is
  no cheap version: it is an offset-surface query, which is precisely the exact
  answer the size gate deliberately declined to compute. The harness does not
  need to be perfect.
- **Making `drops` per-crease** rather than per-node. **Cancelled** for the same
  reason — harness precision that does not change the operator.
- **Groups 1, 2, 4 and most of 5 of `fillet-tests/next-cases.md`.** Most of these
  assert counts, and the queue's own rule is that a count belongs in
  `FilletBuilder_test.cc` where it is milliseconds and exact rather than seconds
  of CGAL dilation. D4 and D5 above absorb what Group 1 was for; T1 absorbs the
  two Group 4 entries worth running. `case_brush_corner_partial` is the only
  survivor worth considering, and only if T2 leaves a question open.
- **`runout=`, variable radius along a chain, and unequal-radius vertex blends.**
  Deferred by the plan and still deferred. A tool node carries one size for the
  whole invocation, so unequal radii cannot currently be expressed at all.
