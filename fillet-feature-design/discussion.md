> **HISTORICAL — input, not spec.** The reframe conversation that produced `REQUIREMENTS.md`. Read
> for rationale; do not implement from it. Current spec: [START-HERE.md](START-HERE.md) →
> [REQUIREMENTS.md](REQUIREMENTS.md).

# Blending feature — design discussion

**Status: discussion record, 2026-08-06. Nothing here is measured, and nothing here is a plan.**
This is a clean-start conversation held after the previous effort was judged off the rails. It
deliberately avoids the vocabulary that effort invented, because that vocabulary only means
anything if you have already accepted its architecture.

No claim in this document has been checked against the existing source. Where a claim about the
current implementation appears, it is inherited from the archived redesign note and is marked as
such.

---

## 1. Why a clean start

The previous effort's terminology — stations, spans, cells, seam covers, canal, wedge, bead,
seating, throat — is not incidental jargon. Every one of those words presupposes a single
architecture: build a tool solid by sweeping a cross-section along a polyline of selected edges,
then boolean it against the model. The words cannot be cleaned without reopening that choice.

The architecture also concedes its own limit. Its remaining defects are junction defects, and a
per-edge local construction cannot fix them by design. An approach whose known-unfixable cases
are its outstanding bugs is a candidate for replacement, not for tuning.

---

## 2. Fixed constraints

These are given, not chosen.

- **Input is an arbitrary imported mesh.** No CSG history, no exact surfaces, possibly
  non-manifold, possibly self-intersecting, possibly badly oriented.
- **Selection is by brush — a solid, not an argument.** OpenSCAD cannot pass predicates over
  geometry, and imported meshes have no named edges. A solid is the only available handle.
  Brushes compose for free: union to widen, difference to carve exceptions, intersection to
  combine criteria.
- **The user wants mix-and-match.** Different regions get different treatments and different
  sizes: this edge rounded, that one chamfered, this region both.
- **OpenSCAD has no negative geometry and no symmetric-difference operator.** Its CSG set is
  union, difference, intersection, hull, minkowski.

---

## 3. The consequence that reframes everything

**The mesh is the surface.**

With no CSG history there is no smooth object underneath the facets to be tangent to. A
tessellated cylinder *is* a fan of flat triangles. Correctness must therefore be defined against
the facets that were supplied.

This single decision removes a whole class of error. The archived note's central complaint — that
contact points placed from averaged normals at an edge land on a different facet, with an error
that does not shrink with tessellation — exists only because the code is trying to be tangent to
a surface that is not there.

---

## 4. What the feature actually is

**A selective, local, bounded offset.**

The 2D case shows why this is the right name. OpenSCAD has no 2D fillet tool; it has `offset()`,
and rounding is a composition of it:

```
offset(r)  offset(-r)  poly            // opening — rounds convex corners
offset(-r) offset(r)   poly            // closing — rounds concave corners
offset(r)  offset(-2r) offset(r) poly  // both
```

`offset(delta, chamfer=true)` gives the flat-cut variant from the same primitive. Four
treatments, one operator, and rounding falls out of morphology rather than being implemented.

The 3D analogue is exact and would resolve everything discussed here — junctions included, since
dilate-then-erode has no special case for a vertex where several edges meet. The reason nobody
ships it is that 2D polygon offsetting is easy and exact while general 3D mesh offsetting is not.
`minkowski()` with a sphere is dilation only, and is the standard example of an operation too slow
to use. Erosion has no primitive at all.

What is tractable, and is the actual feature: **offset restricted to a brush region and to a
distance small enough to stay local.** That frame explains why every hard question in this
discussion — how large is too large, what happens where two blends meet, what happens at a vertex
— is a question about offsetting, and why the previous effort's vocabulary got strange: it was
building a bespoke apparatus for something with a standard name.

---

## 5. API shape

### 5a. The problem

The language has no negative union, so a treatment that both adds and removes material cannot be
returned as one value. Two shapes were considered.

**Tools that return solids** — `round_tool` returns material to remove, `fillet_tool` returns
material to add, and the user applies them with `difference()` and `union()`.

**An operator that returns the blended solid** — `fillet(r) { model; brush; }`, composing by
nesting the way `offset`, `hull`, `minkowski` and `linear_extrude` already do. There is no
language obstacle to this; the precedent is abundant.

### 5b. Where it landed

The stated motivation for the tool form was safety: avoid a black box that swallows the model and
returns something different, with no way to see what changed. That motivation is sound. The
mechanism chosen for it was not — making the user perform the composition moves an invariant onto
them that the language cannot check. Apply one tool and forget the other, or apply them to a
slightly different expression, and the result is plausible and wrong with no error.

The tool form also has a real capability the operator form lacks: **mix-and-match by combining
independently computed tools in one shot.** The objection to it is that this is sound exactly when
it is unnecessary. Two tools computed against the same model in ignorance of each other agree with
the staged result when their regions are far apart, and produce two overlapping approximations
glued together when they are not — silently, because neither computation saw the conflict.

**The resolution (proposed by the owner): give every invocation the whole intended operation set.**

```
difference() { M; round_tool()  { M; round(r=1) brushA; fillet(r=2) brushB; } }
union()      { …; fillet_tool() { M; round(r=1) brushA; fillet(r=2) brushB; } }
```

Both calls receive the same declaration and return different projections of one coherent answer. A
vertex where an r=1 round meets an r=2 fillet is resolved once. Mixed convex/concave junctions stop
being a special case. The tool form keeps its first-class solids and its mix-and-match, and becomes
sound.

Costs and hazards of this shape:

- The full blend is computed once per projection unless the result is shared.
- **Declaration drift** is the new failure mode. If the two invocations disagree, each is
  individually correct and the pair is incoherent, and neither can detect it — each sees only its
  own children. This is the obligation the shape has to answer for.
- It computes a coherent blended result internally. That object need never be returned or even
  fully assembled, but it exists.

### 5c. Diff views

Whichever form is primary, both projections should be available — the material added and the
material removed, as inspectable solids. That is what actually delivers the safety property the
tool form was reaching for: *I can see exactly what this did*. It should be an inspection facility,
not a correctness obligation.

---

## 6. Correctness and refusal

**All four treatments are relational in the same way.** Nothing intrinsic to a returned solid says
whether it is the right size. Correctness is always "does this blend reach geometry it was not
derived from," and answering it always requires looking at the whole model.

An earlier claim in this discussion — that additive treatments are bounded by the model's own
geometry and therefore intrinsically checkable, while subtractive ones are not — is wrong and was
withdrawn. An additive blend on a thin wall punches through and emerges on the far side exactly as
a subtractive one crosses a gap into a neighbouring feature. Same failure, same detection, same
refusal, one criterion for all four.

Consequences:

- "Independently checkable" cannot mean checkable in isolation, for any treatment. It means
  checkable **against the model, without building the full result**.
- The check requires a global spatial query. This is unavoidable in any architecture considered
  here.
- **Refusal must be a measurement, not a threshold** — a distance in millimetres, not a constant
  chosen in advance. Carried forward from the archived note as its one clearly good idea.
- **Refusal must warn.** Silently discarding a region is worse than refusing loudly.

---

## 7. Selection

Selection and blending are separate concerns with separate constants and separate failure modes.
Their conflation is the clearest documented defect in the previous record: one angle threshold was
read at three sites answering three different questions, which is why a size check could never
fire.

Brush-as-solid raises three questions that must be answered explicitly:

1. **The predicate.** Is an edge selected when it lies fully inside the brush, when it overlaps at
   all, or when its midpoint is inside? Users will draw rough brushes and hit the difference
   immediately.
2. **Partial coverage.** A hand-placed brush slices through edges constantly, so this is the
   common case, not an edge case. The blend must either stop square, taper out, snap to a vertex,
   or refuse — and the choice must be stated.
3. **Edges vs. region.** Does the brush select edges (and then blend them entirely), or clip the
   region where blending happens (and blend only the covered part)? These differ exactly on
   partially covered edges.

Position: **clipping**, because it makes the brush behave like the metaphor its name implies, and
because it makes runout a requirement rather than a limitation. The previous effort listed "a blend
that stops partway along an edge stops square" as a known limitation; under brush-as-solid that is
not a limitation, it is the default situation, and a visible step mid-edge on ordinary use.

---

## 8. Options considered and set aside

**Negative union / signed geometry in the language.** Technically possible; BOSL2 demonstrates the
pattern in userspace with tagged subtrees resolved by an enclosing operator. Set aside because
OpenSCAD's union is implicit — adjacent children in *any* block union — so negative geometry would
leak into every block, module and loop. Changing what `{}` means language-wide to serve one feature
is a hard sell. More importantly, it would let the composition be *expressed* without making it
*correct*: the ordering semantics for several signed solids landing on one model is the same
interaction problem, relocated somewhere harder to reason about.

**Symmetric difference (XOR).** Not in the language; would be written as
`difference(union(A,B), intersection(A,B))`, three booleans. Attractive because XOR is an
involution: if the tool returns the symmetric difference between the model and the blended model,
then applying it once recovers the blended model exactly — one operator, one solid carrying both
added and removed material, no sign representation needed. This is the negative-union capability
arriving through the back door. Two obstacles: the tool's boundary coincides exactly with the
model's over the whole contact region, so cancellation at coincident faces becomes central rather
than incidental; and any epsilon offset introduced to make the boolean well-conditioned breaks the
exactness the involution depends on. The two fixes are in direct tension.

**Topological bevel (mesh edited directly, no boolean).** Lift out the faces near selected edges,
insert the blend surface, sew. Untouched geometry passes through verbatim; cost is linear in
selected edges rather than in model size, which inverts `minkowski()`'s behaviour. It deletes the
boolean entirely, and with it the epsilon overshoot, the coincident-face fragility, and the batch
boolean cost — all of which exist only to survive a kernel. Not set aside; it is the leading
implementation candidate for the bounded offset of §4. Its cost is that **you own validity**: a
boolean kernel, for all its expense, resolves overlapping blends and oversized radii into
*something*, whereas editing directly means detecting and refusing those yourself and producing a
manifold result unaided. On input that may already be non-manifold, that is the hard part of the
design. It is also work that refusal-as-measurement requires anyway.

---

## 9. Open questions

1. **The four treatments.** Assumed here: round and chamfer for convex edges (arc / flat, removing
   material), fillet and bevel for concave edges (arc / flat, adding material), parameterised by
   radius and setback respectively. Not confirmed.
2. **Whether the tool form or the operator form is primary**, given that §5b makes the tool form
   sound. Both can exist; which one the documentation leads with is a real decision.
3. **Declaration drift** (§5b) — is the operation set written once and shared, or is there a check
   that the invocations agree?
4. **The selection predicate and partial-coverage behaviour** (§7).
5. **How chamfer and fillet parameters relate.** With a setback for one and a radius for the other,
   the two coincide only at 90°; band widths move in opposite directions as the angle sharpens, so
   the two numbers are not interchangeable and the documentation must not imply they are.
6. **Whether variable size along an edge is in scope.** Runout (§7) requires the blend to reach
   zero, which is variable size in the degenerate case.

---

## 10. Next step

Write the requirements: what the feature promises, what it refuses, what "correct" means, what
happens at brush boundaries — in plain geometric language, with no method presumed. Choose the
algorithm against it, not before it.

Open question 1 gates the draft.
