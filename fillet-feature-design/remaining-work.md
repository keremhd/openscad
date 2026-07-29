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
2. **D1 and D6 together** — both live in the size gate's contact test.
3. **D2** — no longer merely cosmetic. T1 showed the same slivers make the
   operator's own output unreadable by the operator, so this is worth more than
   the timebox it was given.
4. **M12** — the wrapper.
5. **T2** — the last case worth building.
6. **DOC**, then **CLEAN**.

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
classifier disagree with the shape it just built. So reducing the tangential
contact is not a test-only fix worth a timebox and no more — it is the one change
that makes the operator's output re-readable by the operator. Weigh D2
accordingly, and if the phase shift lands, re-run this test and record what the
counts become.

---

## D1 + D6 — the size gate's contact test

Both live in `chainContacts`. Do them in one pass.

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

## D2 — the bare rounded tool will not convert to Nef

At every corner a sphere sits tangent to three walls, and the surfaces meeting
along those tangencies leave triangles too small to survive CGAL quantising its
input. *Applying* the tool is fine on both backends; the bare tool is not, so
`round_tool(r=2) cube();` under `--backend=cgal` renders with facets dropped.
`render-cgal_round-tool-tests` and `render-csg-cgal_round-tool-tests` are
disabled for it, and seven `sandwich` checks in `fillet-tests/` are red for it.

**The approach to try: phase the arc tessellation so a vertex lands past the
wall, rather than a facet grazing it.** It is local to the section construction,
changes no radius, and removes the sliver at its source.

Rejected alternative, for the record: growing the ball by an eps so it cuts into
the wall instead of kissing it. That loses tangency by about 2.6 degrees at
`r = 3, eps = 1e-3` and perturbs the blend everywhere, to fix a problem that only
shows on the bare tool.

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

**Timebox this.** The fallback is exactly the status quo, whose reasoning is
already recorded in `tests/CMakeLists.txt` and `fillet-tests/expectations.txt`. If
the phase shift does not take, leave it and move on — do not escalate into the
circular-segment rewrite (see Cancelled, below).

**Acceptance:** the two disabled CTests are re-enabled and pass; the pocket and
apex `sandwich` lines in `expectations.txt` go green and are deleted. The applied
results do not change beyond tessellation phase — the exact comparisons in
`FilletCompare_test.cc` must stay green untouched.

---

## M12 — the `fillet()` wrapper

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

## T2 — `case_two_bosses`, plus a dome variant

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
  possible route to D2. D2 has a cheaper route. If the phase shift fixes D2, this
  has no remaining reason to exist.
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
