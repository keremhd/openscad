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
6. **BRUSH-WIDTH** — two cheap builder tests and a comment, pinning behaviour the
   doc page now relies on. Independent of everything else; do it whenever.
7. **DOC**, then **CLEAN**.

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

## BRUSH-WIDTH — pin what a narrow brush does, and say it on purpose

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

**Do:**

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
