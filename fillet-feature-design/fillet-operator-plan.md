# OpenSCAD Fillet / Round / Chamfer Operator — Implementation Plan

Target: new C++ nodes in OpenSCAD that generate **fillet tool solids** from an
existing mesh, with **brush-based edge selection**.

This document is the result of a long design pass. It records both what to build
and — equally important — the approaches that were considered and **rejected**,
with reasons. Read [§9 Do Not Do This](#9-do-not-do-this) before writing code.
Several items there are traps that look correct and produce plausible-but-wrong
geometry.

> **Status.** This is the design and it still holds. It is **not** the to-do
> list: most of it is built. For what is left, read
> [`remaining-work.md`](remaining-work.md), which supersedes this document
> wherever the two disagree — notably §8 (re-fillet tagging) and §11 (analytic
> fast paths), both cancelled, and §2.1 (the wrapper as bundled SCAD), overruled
> in favour of a C++ node.

---

## 1. Core concept

The operator does **not** return a filleted object. It returns a **tool solid**
that the user applies themselves:

```openscad
union()     { thing(); fillet_tool(r=2) thing(); }   // inner fillets
difference(){ thing(); round_tool(r=2)  thing(); }   // outer rounds
```

This is deliberate. It means we never have to produce a valid *filleted* mesh —
only a valid *tool*. The user's boolean does the hard part.

### The W − U decomposition

The tool's cross-section is "corner minus arc", which is **non-convex**. It is
built as the difference of two well-behaved solids:

| solid | role | what it is |
|---|---|---|
| **U** | shapes the curve | union of all rolling balls (the canal solid) |
| **W** | bounds the extent | the wedge from the edge out to the tangency lines |

```
tool = W − U
```

- U contributes the concave arc surface, and nothing else. It is much larger
  than W and sticks out everywhere; that's fine, W trims it.
- W decides where the fillet starts and stops — both across the faces and along
  the spine.

**Sanity check** (keep this as a regression test): cube with a through-hole,
plane ⊥ cylinder axis, hole radius `R`, fillet radius `r`. Then φ=90°, `d = r√2`,
the ball centers trace a circle of radius `R+r` at `z = −r`, so U is a torus and
W is the annular prism `R ≤ ρ ≤ R+r`, `−r ≤ z ≤ 0`. The tool is
`annulus_prism − torus` — exactly the idiom people hand-write in OpenSCAD today.
The general algorithm must degenerate to this.

### Chamfer is the same code with U omitted

`W` alone is a chamfer tool. One flag. See §6.

---

## 2. Node inventory

| node | children | returns |
|---|---|---|
| `fillet_tool(r, ...)` | 0 = target, 1+ = brushes | concave tool (user **unions**) |
| `round_tool(r, ...)` | 0 = target, 1+ = brushes | convex tool (user **subtracts**) |
| `chamfer_tool(t, ...)` | 0 = target, 1+ = brushes | concave chamfer tool (union) |
| `bevel_tool(t, ...)` | 0 = target, 1+ = brushes | convex chamfer tool (subtract) |

Convexity sign is **baked into the node name**, not passed as a parameter. The
caller applies the boolean, so a tool of mixed convexity would be unusable.

Child 0 = subject, children 1+ = operands. This matches `difference()` and it
matches the `attach`-style convention, so the nodes compose predictably.

Common parameters:

- `r=` / `t=` — radius or setback
- `min_angle=` — override the auto-derived dihedral threshold (§4.3)
- `ignore_tags=` — `true`, or a list of tag names; allow re-filleting (§8)
- `tag=` — name this pass's output so it can be targeted later (§8)
- `$fa/$fs/$fn` — drive arc/sphere tessellation

### 2.1 The geometry-modifying wrapper — ship it as OpenSCAD, not C++

Most users want one call that applies both signs and hands back modified
geometry. That is **script sugar over the four tool nodes**, distributed as a
bundled `.scad`. Do not implement it in C++.

```openscad
module fillet(r=2, inner=true, outer=true) {
    difference() {
        union() {
            children(0);                                // target only
            if (inner) fillet_tool(r=r) children();      // target + brushes
        }
        if (outer) round_tool(r=r) children();
    }
}
```

`children(0)` is the target; bare `children()` forwards target *and* brushes to
the tool nodes, which is exactly their calling convention. An `if` yielding no
geometry inside `union`/`difference` is fine.

Why script rather than a fused C++ node:

- **The saving isn't where you'd think.** A fused node would share one edge
  classification pass. But classification is O(edges) with a dot product and two
  branches per edge — microseconds on a 10k-triangle mesh. The cost is in the
  hulls and the `BatchBoolean`, and those are *not* shareable: the concave and
  convex tools are built from disjoint chains and produce different geometry.
  Sharing analysis saves a rounding error.
- **The repeated `children()` is nearly free.** Three instantiations hash to the
  same id string, so the geometry cache serves 2 and 3 (see §11). Verify this
  actually holds for `children(0)` vs `children()` when `$children == 1` — the
  id strings may differ by a `group()` wrapper, in which case the target is
  evaluated twice. If so, wrap both consistently.
- **Users can read and fork it.** Someone who wants union-before-difference
  reversed, or only the outer pass, or a different tool composition, edits six
  lines instead of filing a feature request.

Revisit the C++ version only if the two passes need to *coordinate* — the case
being a vertex where a concave and a convex chain meet, which needs both chain
sets in hand simultaneously. That's the same problem as unequal-radius vertex
blends, and it's deferred (§9).

### Placement in the codebase

Model on `hull()` / `minkowski()` — the existing precedent for nodes that
consume child geometry rather than transform it.

- New node class in `src/core/`
- Register in `Builtins`
- Evaluate in `GeometryEvaluator`, alongside the `CgalAdvNode` cases
- Pull children as `ManifoldGeometry`, analyse `MeshGL64`, emit `ManifoldGeometry`

---

## 3. Usage model: apply locally, in small chunks

Fillets are applied **at the point the feature is created**, not at the end of a
big assembly:

```openscad
module plate() {
    p = [25, 25, 0];
    difference() {
        cube([50, 50, 10]);
        translate(p) cylinder(r=5, h=12, center=true);
    }
    fillet_tool(r=2) { ... }   // or the fused fillet() wrapper
}
```

Two reasons this is right:

1. **The mesh is small.** Analysis runs on a cube and a cylinder, not on a 40k
   triangle assembly. The analysis pass is the part that scales badly with mesh
   density, so local application is *cheaper*, not just tidier.
2. **The brush is already in your hand.** It's usually the cutter, resized —
   in local coordinates, three lines from the thing that motivated it, before
   any assembly transform has moved it.

Consequence: **nesting is the normal case**, so re-fillet protection (§8) is
mandatory, not optional.

Rule for users to learn: *fillet after that edge is final.* A later
`difference()` through a filleted edge is fine (you see the arc in section). A
later `union()` that fills the same concave corner buries the bead in the
interior, where Manifold discards it — silent and wasted.

---

## 4. Edge classification

### 4.1 Iterate edges, not face pairs

In a manifold mesh every edge has exactly two incident faces. Iterating edges
gives adjacency for free.

### 4.2 Concavity test

`dot(nA, nB)` **cannot** distinguish inner from outer — a 90° convex edge and a
90° concave edge both give φ=90°. You need a separate test.

Take a vertex of face A that is *not* on the edge:

```
concave = dot(nB, a_far - p_edge) > 0
```

"My neighbour's far corner pokes out in front of my plane" → concave.

Verify against a cube (convex, −1) and an inside corner (concave, +1) in unit
tests.

### 4.3 Angle filter

```
φ = acos(dot(nA, nB))          // 0 = flat, larger = sharper crease
```

The threshold must be **derived from the tessellation parameters**, not a magic
constant. OpenSCAD's `get_fragments_from_r` is:

```
$fn > 0  →  max($fn, 3)
else     →  ceil(max(min(360/$fa, r·2π/$fs), 5))
```

So `$fa` is a hard upper bound on any seam OpenSCAD generates. Therefore:

```
φ_min = 1.5 * max($fa, $fn ? 360/$fn : 0)
```

Default `$fa=12` → 18°.

| edge | φ | verdict |
|---|---|---|
| bore seam, `$fn=64` | 5.6° | skip ✓ |
| bore seam, `$fn=16` | 22.5° | fillet ✗ (see below) |
| existing fillet arc seam | 12° | skip ✓ |
| existing tangency crease | 6° | skip ✓ |
| hole mouth | 90° | feature ✓ |

The `$fn=16` row is the honest miss. Usually the same `$fn` is in scope at the
fillet node, so `360/16 = 22.5` feeds the formula and the threshold rises to
34°. It only fails when `$fn` is set on the primitive and invisible to the
operator — a documented case with an obvious `min_angle=` override.

Note this is *self-consistent* for re-fillet: our own arc seams are generated at
`$fa`, so the next pass rejects them by the same rule that generated them.

### 4.4 Provenance test (upgrade, when available)

Manifold propagates face origin through booleans. This is strictly better than
any angle test because it is tessellation-independent:

```
both faces same originalID  →  same primitive's surface  →  skip
different originalID        →  boolean seam              →  candidate
```

On `cube − cylinder`: the mouth edge has one cube face and one cylinder face
(different IDs → candidate); bore seams have two cylinder faces (same ID →
skip). Correct at every `$fn`.

Use provenance where present; **fall back to the §4.3 threshold** where it isn't
(imported STL, `polyhedron()`, anything post-`hull`).

Build §4.3 first (ten lines, covers the default path), layer §4.4 on top.

---

## 5. Spine construction

### 5.1 Chain walking

Walk tagged edges into chains via shared vertices. Chains matter because ball
centers must be computed **per chain vertex**, not per edge (§9).

Handle: open chains, closed chains (the hole-mouth case), and branch vertices.
Junctions of any valence with equal radii get real handling (§6.3, §6.3.1);
runout (§6.3.1) is the fallback for unequal radii or degenerate normals.

**Chain ordering must be canonical** — sort by something geometric, not by mesh
traversal order — or any downstream index-based reference shuffles on every
parameter nudge.

### 5.2 Per-vertex averaged normals

At each chain vertex, average the face normals on each side:

```
nA_v = normalize(Σ side-A face normals at v)
nB_v = normalize(Σ side-B face normals at v)
```

This is what makes the ball-center polyline continuous across the chain.

### 5.3 Ball centers and tangency points

```
φ_v = acos(dot(nA_v, nB_v))
d   = r / cos(φ_v / 2)
b   = normalize(nA_v + nB_v)      // concave: points into the air
C   = v + d * b
TA  = C - r * nA_v
TB  = C - r * nB_v
```

Setback works out to `t = r · tan(φ/2)`.

**Check on floor `z=0` (nA=+z) / wall `x=0` (nB=+x), edge along y:**
`d = r√2`, `C = (r, y, r)`, `TA = (r, y, 0)` on the floor, `TB = (0, y, r)` on
the wall. Wedge triangle is `(0,y,0), (r,y,0), (0,y,r)`.

**Guard:** as `φ → 180°`, `d` blows up. Clamp or skip.

### 5.4 Use the mesh's own vertices as the spine

Do not resample. Using actual mesh vertices means W's flank faces land exactly
coplanar with the model's facets, and the tool's faceting matches the model's at
the same `$fn`. No mismatch scallops at the join.

---

## 6. Building the two solids

### 6.0 Cell inventory — what gets hulled with what

Everything below is one of three cells. Nothing else exists.

| cell | where | `U` contribution | `W` contribution |
|---|---|---|---|
| **edge cell** | between consecutive spine stations | `hull(section[v], section[w])` | `hull(pentagon[v], pentagon[w])` |
| **junction cell** | where ≥3 chains meet | `hull(sphere(r) at every feasible Q-vertex)` — one hull over all of them | corner cell (§6.3) |
| **open end** | part boundary, brush clip | **nothing** — extend the chain one segment and stop | flat perpendicular cap |

**The joins need no hull.** A spine ends *at* a Q-vertex, so the junction's
sphere is centred on the chain's last station and swallows its end section. Union
is sufficient and correct. Likewise a closed chain simply wraps; no special case.

**Not every chain end gets a sphere — only junctions do.** A sphere at an open or
brush-clipped end bulges past the flat cap and scoops a dish out of `W` (§7.2).
This is the single easiest thing to get wrong when generalising "put a sphere at
the end."

Read §6.3.1 for why the junction rule is one hull over several spheres rather
than one sphere: at valence 3 there is exactly one feasible Q-vertex and the
hull degenerates to that single sphere, so the same code covers both.

### 6.1 U — hull circular segments, not spheres

**Start with a full disc.** The simplest correct section is the disc of radius
`r` centred at `C`, perpendicular to the spine; hulling consecutive discs gives
the canal solid directly and needs nothing but `C` and the spine direction. Get
that working before optimising — it is the fastest route to a fillet on an
isolated edge.

The optimisation below is worth taking afterwards, not first.

Only a thin sliver of each rolling ball is ever inside `W`, so building `U` from
full spheres — or full discs — wastes most of the geometry. Use the **circular
segment**: the 2D region between the chord `TA–TB` and the arc of radius `r`
centred at `C`, lying in the cross-section plane. `TA` and `TB` are already in
that plane, since both are offsets from `C` along face normals, which are
perpendicular to the spine.

```
U += hull(segment[v], segment[w])
```

Why the segment is exactly right: the wedge cross-section is the triangle
`(v, TA, TB)`, and the arc is inscribed in that corner — tangent to both legs at
`TA` and `TB`, bulging toward `v`. So the triangle's third side *is* the chord,
and

```
fillet cross-section = triangle (v, TA, TB) − circular segment
```

A circular segment is always convex, so the hull is faithful. Consecutive hulls
share the segment face, so the union is continuous; on the outside of a bend you
get a facet crease of the same order as `$fn`, not a gap.

Cost: ~10 vertices per station instead of a few hundred per sphere, and `U`
stops being a huge solid that sticks out everywhere. Tessellate the arc from
`$fa/$fs`; approximation error is `r(1 − cos(Δφ/2))`, same character as `$fn`.

Self-intersection where the spine curves tighter than `d` still dissolves into
the union rather than producing a broken loft — the reason for using booleans
over a swept loft is unchanged.

**Spheres are still needed at branch vertices** (§6.3). The segment is a
cross-section perpendicular to one spine; a trihedral corner needs the actual
ball (§6.3).

**Why the bare tool cannot be quantised, and why `eps` does not answer it.**
Worth recording, because the obvious response — "push it past by an `eps` like
the pentagon does" — is already the implementation, at both of the places that
need it, and the failure is downstream of that.

These builtins emit a *tool solid*: the sliver you subtract, not the finished
shape. `round-tool-tests.scad` renders that sliver on its own so a regression in
it is visible rather than hidden under the model, which is why only the bare form
is red. **It is not a knife edge.** The pentagon's `TA'→TA` edge runs along `nA`,
perpendicular to wall A, while the disc's arc is tangent to wall A at `TA` — they
meet at 90°, and `eps` is exactly what buys that. What is left is a **ribbon of
width `eps`** along the whole tangency line, and at a corner cell a sphere
crossing the cell's wall face (set at `eps/2`) in a circle of radius `√(r·eps)`
at an incidence of `√(eps/r)`. At `eps = 1e-3·r` that ribbon is 2 µm on a 2 mm
fillet. CGAL quantises coordinates onto an absolute grid when converting to a Nef
polyhedron, so these vanish regardless of how cleanly they cross — the M8 note is
precise in saying *triangles too small*, and it is a size problem, not an angle
one. Manifold does not quantise and is unaffected. *Applying* the tool is fine on
both backends: the boolean consumes the ribbon and CGAL is handed the finished
solid, where the flat face runs past the tangency at full width.

**`eps` is a bracket, not a knob**, which is why enlarging it is not the fix:

- The corner cell's margin must be strictly *less* than the edge cells' `eps`, or
  its wall face lands in their plane and the coincidence returns inside the tool.
  It is the midpoint of a two-sided bound, not a free parameter.
- `eps` is how far the tool overshoots the model's wall, so growing it is real
  error in the delivered cut and hands the caller's boolean a deeper sliver.
- Decisively: **the margin that would be safe is not a constant.** The hull
  between two stations overhangs each station's plane by an amount set by how the
  spine turns — 0.3 mm on the boss base at `$fn = 48`. M7 measured this by
  raising the chord margin to 2 % of `r`, twenty times the wall `eps`: it rescued
  the hole mouth and never rescued the boss base at any margin tried.

The remaining candidate is therefore to reduce the *amount* of grazing contact
rather than widen the separation — the circular segment, which replaces the
tangent face with a chord that crosses. **But note what it does not reach.** The
segment removes the ribbon along the edges; the corner ball is a sphere tangent
to three walls either way, so the `√(r·eps)` cap survives it. M8 recorded the
segment as the fix for the corner failure; that attribution is plausible and
**untested**, and testing it is the first step of reopening the question, not a
detail of carrying it out.

### 6.2 W — the wedge, with pentagon cross-section

The naive triangle `(v, TA, TB)` sits exactly coplanar with the model's faces.
Fix by offsetting each tangency point along **its own** face normal:

```
TA' = TA - eps * nA
TB' = TB - eps * nB
v'  = v  - eps * b

section = [TA, TB, TB', v', TA']          // convex pentagon
```

Per segment:

```
W += hull(section[v] ++ section[w])       // 10 points
```

Now the tool crosses face A at `TA` going straight down at **90°** — the
best-conditioned crossing you can hand a boolean kernel. The outer face `TA→TB`
is untouched, so visible geometry is unchanged.

**Why this is safe:** at a concave edge the material is the *union* of two
half-spaces (270°), so a point only needs to be behind *one* plane. With
`t=1, eps=0.1`: `TA'=(1,−0.1)` is below the floor; `TB'=(−0.1,1)` is *above* the
floor plane but at `x<0` so it's inside the wall; `v'` is inside both. Each
offset point only answers to its own face, so `eps` has a lot of slack.

**Convex mirrors exactly** — flip all three signs, so the tool pokes into air:

```
TA' = TA + eps*nA ;  TB' = TB + eps*nB ;  v' = v + eps*b
```

General rule: **displace in whichever direction the user's boolean will absorb.**
Union → into material. Difference → into air.

`eps = 1e-3 * t`, clamped to a floor. Relative, so it scales with the feature.

### 6.3 Junction cells — a required cell type, not an extra

Three concave edges meeting at a vertex is every inside box corner, every square
pocket floor, every rib landing in a corner. A chain has **two kinds of cell**:
edge cells between consecutive spine vertices (§6.1–6.2), and junction cells
where chains meet. Building only edge cells leaves the feature incomplete.

Degree 3 is below; §6.3.1 generalises it to any valence, and is worth reading
first for the framing that makes both cases one construction.

Three things are needed.

**1. The corner ball centre `P`.** Solve, for the three incident faces,

```
n_i · (P − p_i) = r          i = 1,2,3        // 3×3 linear solve
```

`P` lands on all three spines automatically: each spine is the intersection of
two offset planes, and `P` lies on all three. Degenerate when the normals are
linearly dependent — detect and fall back to split-and-warn.

Symmetric check: faces `x=0, y=0, z=0` with outward normals `+x, +y, +z` give
`P = (r, r, r)`, and the z-edge spine `C(t) = (r, r, t)` passes through it at
`t = r`.

**2. Truncate the spines *and the wedges* at `P`.** Both. Truncating only `U` is
wrong: past `P` the ball cannot reach (it would penetrate the third face), so if
`W` runs on to the geometric vertex there is nothing left to subtract and you get
a spurious block of material filling the corner.

Do not special-case this to degree 3. State it as the universal rule it is:

```
truncate each spine where dist(C, f) < r first holds
for any face f incident to the junction
```

The ball must stay out of the material; that is the whole constraint. It is
always computable, needs no solve, and degenerates correctly at every valence —
at degree 2 nothing binds, at degree 3 all three spines bind simultaneously at
`P`, at degree 4+ they bind at different points. `P` then stops being a special
construction and is just the name for the case where the truncation points
coincide, which is what lets the corner close exactly.

**3. Add a corner cell.**

```
W_corner = hull(v, TA, TB, TC, and the three truncated wedge end-sections)
tool    += W_corner − sphere(r) at P
```

In the symmetric case `W_corner` is the box `[0,r]³` and `W_corner − ball(P)`
gives, at every height, exactly the cross-section the three edge cells produce at
their own truncation planes — verified at `z = 0.1r`, where the correct answer is
the square minus a quarter-disc of radius `√(r² − 0.81r²)` at the far corner.

Budget roughly 100 lines. Compare with a B-rep kernel, where setback vertex
blends have no closed form and consume most of the fillet code — that remains
the real advantage, just not a one-liner.

### 6.3.1 Degree ≥ 4 — hull the feasible triplet spheres

This closes exactly. It is not an approximation and not out of scope.

**The unifying construction.** Let `Q` be the set of legal ball centres near the
junction: the intersection of the incident faces' half-spaces, each offset
inward by `r`.

```
Q = { p : n_i · (p − p_i) ≥ r  for every incident face i }
U = Q ⊕ ball(r)                                    // Minkowski
```

`Q` is convex, so `∂U` is G1 by construction, and every piece of the pipeline is
a boundary feature of `Q` dilated:

| feature of `Q` | dilated to | built as |
|---|---|---|
| edge | canal / cylindrical strip | segment hulls along the spine (§6.1) |
| vertex | spherical patch | `sphere(r)` at that vertex |
| face | plane offset back to the original face | (tangency; nothing to build) |

The §6.3 truncation rule is the same statement: a spine is an **edge** of `Q`,
and it ends where `Q` has a **vertex**.

**Degree 3** gives `Q` a single vertex `P` — hence one sphere.

**Degree 4+** generally gives `Q` several vertices, because the four offset
planes no longer share a point even though the four original faces did. Those
vertices are exactly the triplet solutions:

```
for each triple of incident faces:
    solve the 3×3 for P_ijk
    keep it only if n_m · (P_ijk − p_m) ≥ r  for every other incident face m
U_corner = hull( sphere(r) at each kept P )
```

The feasibility filter is the load-bearing part. An infeasible triplet solution
is tangent to its three faces and penetrates a fourth; using it gouges the
fillet back from that face. Discard rather than clamp.

Hulling the kept spheres is exact, not approximate: `hull(balls at vertices)` =
`hull(vertices) ⊕ ball`, and `hull(vertices)` is the tip of `Q` because `Q` is
convex.

**Symmetric configurations are the easy case, not the hard one.** A symmetric
square pyramid pocket has all four offset planes meeting on the axis, so `Q` has
one vertex and a single sphere suffices. It is *asymmetric* valence-4 junctions
that produce multiple vertices — which is the case this construction handles.

**Unequal radii remains out of scope.** Different `r` per edge means `Q` is not
a single constant offset, and the whole framing collapses. Detect and warn, with
the runout fallback below.

**Runout as a general fallback.** Where a junction is rejected for any reason —
unequal radii, singular geometry, no feasible vertex — ramp `r` toward zero over
the last few segments of each incident chain. All beads converge on the sharp
vertex, the corner closes with no patch, and the result is a valid solid whose
fillet fades out locally. Reuses the `runout=` machinery (§12) and needs nothing
new, since `C`, `TA`, `TB` are already per-vertex functions of a per-vertex
radius.

Never fall through to undefined output, and never solve the 3×3 on an arbitrary
subset without the feasibility check.

### 6.3.2 Numerical guards

The degenerate branches in this pipeline all produce non-finite or absurd
coordinates rather than clean failures, and Manifold will either throw or
consume unbounded memory building a hull around a point at 1e30. Guard at the
boundary:

- **Singular 3×3.** Three faces whose normals are linearly dependent (e.g. all
  parallel to one axis) make the solve singular. Check the determinant or
  condition number *before* solving; reject below threshold.
- **`d = r / cos(φ/2)` as φ → 180°.** Already flagged in §5.3; same family.
- **Sanity bound.** If `|P − v| > 10r`, the configuration is near-degenerate
  even if the solve nominally succeeded. Reject.
- **Finite check.** Assert every emitted coordinate is finite before it reaches
  Manifold. Cheap, and it converts a class of silent corruption into a
  diagnosable error.

A rejected junction falls back to the ladder above, not to undefined output.

### 6.3.3 What does *not* work: hulling the end cross-sections

Tempting, since the three chains already terminate at `P` with a circular
segment each, and those segments' arcs lie on `sphere(P, r)`: hull the three end
segments and skip the sphere.

It fails, and the `Q` framing says why — the corner contribution comes from a
**vertex** of `Q`, which dilates to a solid spherical patch. Cross-sections
through that ball do not reconstruct it, because a convex hull of curves lying
on a sphere always inscribes, and the worst deficit is in the middle of the
patch, which is exactly the corner.

Symmetric case, worked: the three arcs meet the faces at `TA=(r,r,0)`,
`TB=(0,r,r)`, `TC=(r,0,r)` around `P=(r,r,r)`, and their midpoints are
`(0.293r, 0.293r, r)` and cyclic. Those midpoints span the plane
`x+y+z = 1.586r`, which sits `0.816r` from `P` against a true radius of `r`.

**Deficit ≈ 0.18r**, versus `0.0055r` for a sphere at `$fa = 12` — two orders of
magnitude worse. Tessellating the arcs more finely does not help, since it adds
no points in the interior of the spherical triangle. `U` too small means the
tool is too big, so this shows as a lump protruding at every corner.

Note the cost argument does not apply here either. Segments beat spheres along
chains because chains have hundreds of stations; junctions number in the
handful, so a full `sphere(r)` per corner is a few thousand triangles across a
whole model. Use the sphere.

### 6.4 Chamfer mode

`W` alone. Skip the ball entirely and take the setback **directly** — do not
derive it from `r`, because `t = r·tan(φ/2)` means `r` behaves as an inscribed
radius (0.58r at 60°, 1.73r at 120°), which is not what anyone means by
`chamfer(2)`.

Get the in-face directions by Gram-Schmidt:

```
c  = dot(nA, nB)
uA = normalize(nB - c*nA)      // lies in face A
uB = normalize(nA - c*nB)      // lies in face B
TA = v + tA * uA
TB = v + tB * uB
```

(Correct for concave; negate both for convex.)

`tA` and `tB` are independent → asymmetric chamfers are free.

**Build the chamfer path first.** Same scaffolding, exact at any tessellation,
no sphere meshes, no big union — and crucially **no grazing contact**, because
the chamfer face meets the original faces transversally rather than tangentially.
It's the fillet with the numerically nastiest part removed.

### 6.5 Validity

`tA` cannot exceed the distance from the edge to the far side of face A, or the
tool eats the next feature. Check per chain vertex. Similarly `r` must be ≤ the
local radius of curvature and ≤ half the distance to the nearest other feature.

**Out of range means warn and drop that chain. Do not clamp, and do not vary the
size automatically.** Never emit garbage geometry.

The alternative — clamp to the largest size that fits — does not survive contact
with a corner. Clamp edge A to its tight vertex and the blend where A meets B now
has two sizes to reconcile, so either B clamps too or the corner is undefined.
Follow that to its fixed point and the `min` runs over the whole connected
network: one tight vertex in a corner of the part silently shrinks a fillet on
the opposite side, and nudging that one dimension by 0.1 mm resizes geometry the
user is not looking at. §9 already lists discontinuous neighbour-set membership
as a usability hazard; a network-wide `min` makes it nonlocal as well, which is
the worse half. Discarding rather than clamping is also what §6.3.1 already does
with infeasible triplet solutions, so the two agree.

"Just clamp to the largest size that fits" also assumes the largest size is cheap
to know. Two of the three constraints above are local, but "half the distance to
the nearest other feature" is a global proximity query and a circular one — what
counts as the nearest feature depends on how far the tool reaches, which depends
on the size being solved for. Computing a true maximum costs about as much as
building the tool and testing it. Clamping does not avoid that work; it hides it
and then hides the result too.

Variable size along a chain is a legitimate *feature* — sizes given per chain
vertex and interpolated, tangency matched where chains meet — and a bad error
recovery. As a feature the user asked for the variation; as a fallback they get
geometry they did not ask for and cannot see is wrong. Keep it for later, and
keep it explicit.

One exception, for float noise rather than intent: when a size exceeds a limit by
less than the `eps` of §6.2, clamp to the limit silently. Dropping a fillet over
1e-9 would be its own bug.

`fillet-tests/` encodes this decision directly: a case variant declares `kind =
"drop"`, and the `drops` check requires both an empty tool and the warning.

**The sampled gate compensates for a blind spot it created itself.** The
implemented check asks its two questions at points, and the exemptions it needs
are what make the sampling necessary — this is worth stating plainly, because
read separately each rule looks like independent tuning.

The touching question cannot be asked at the end of an open chain, or within
`2r` of a junction: a crease stops at the boundary of its own walls, so stepping
in perpendicular from near a corner leaves through the *neighbouring* crease's
face — a corner, not an overshoot. On a tetrahedron the base corners are 60°, so
this is guaranteed, and every cone in the suite was refused until the exemption
went in. The crowding question is separately exempt between chains that share a
vertex, because sharing material at a vertex is what a corner cell is.

Apply both to a `$fn = 3` cone. Its slant crease is a straight mesh edge with
exactly **two** stations, base corner and apex; both are chain ends, both are
degree-3 junctions, and all three slant creases share the apex. Every station is
exempt from both questions. Without interior samples that crease is asked
*nothing at all* — the taper is not something the ends fail to notice, it is
something nothing is looking at. The interior samples are the only probe that
reaches back into the excluded stretch, which is why the walk has to scale with
the size (`r = 1` is caught 27 mm below the tip; `r = 0.5` moves the failure
above a fixed three samples).

So the rule stands as written — one sample per size along a segment, at least
three, capped at 32 — but the reason to prefer the exact replacement is stronger
than "the sampled version is coarse". An exact check (build and test, or a
distance field on the offset surface) needs **no corner exemption at all**,
because it is not probing a proxy quantity that misreads at corners; removing
the exemptions removes the blind spot the sampling exists to patch, and the
three tuning constants go with them. That is the argument for doing it, and it
is independent of whether the sampled gate is ever caught being too coarse.

---

## 7. Brush selection

Brushes are ordinary CSG solids passed as children 1+ (unioned together). They
select *a portion of the edge curve*.

```openscad
fillet(r=2) {
    my_object();
    translate([0,0,10]) cube([50,50,5], center=true);
    cylinder(r=8, h=100);
}
```

Because brushes are ordinary CSG, negative selection is free:
`difference() { big_brush(); keep_this_edge_sharp(); }` — no new syntax.

### 7.1 Intersect the spine, not the volume

Per candidate segment `(v,w)`: raycast `v → w` against a BVH over the brush
triangles, collect hit parameters in `[0,1]`, sort. Parity-test `v` once per
chain, then alternate. Result: a list of intervals in spine-parameter space.

At a clip parameter `t`, **lerp the cross-section points directly**:

```
v_t  = lerp(v,     w,     t)
TA_t = lerp(TA[v], TA[w], t)
TB_t = lerp(TB[v], TB[w], t)
```

This is exact, not approximate: `hull(section_v, section_w)` *is* the linear
interpolation of the cross-section along the segment, so slicing the prism at
`t` and building a prism to the lerped section give the same solid.

### 7.2 Clip W. Do not clip U.

`U`'s capsule has a **round cap**. Shortening U to end at `C_t` makes that
hemisphere bulge back past the cut plane and scoop a dish out of W's flat end
face.

```
W over [t0, t1]
U over [t0 − 1 segment, t1 + 1 segment]
```

Overhanging U is harmless anywhere W doesn't exist.

### 7.3 What this buys

- **Tessellation independence** — the fillet ends at a fixed physical point;
  changing `$fn` on the target doesn't move the termination.
- **No vertex flip-flop** — a brush boundary landing on a spine vertex is a
  continuous parameter near 0 or 1, not a coin flip.
- **Flat perpendicular cap** — always the full cross-section. Lengthwise
  "shaving" of the bead is structurally impossible.

### 7.4 Guards

- **Minimum interval length** — drop intervals shorter than some fraction of `r`.
  A brush grazing the spine tangentially produces two crossings a hair apart and
  a 0.02 mm stub of bead. Never intentional.
- **Empty selection** — warn with counts:
  `WARNING: fillet: brush selected 0 of 47 candidate edges`. Do not silently
  no-op; the user will assume the radius is wrong.
- **Grazing boundaries** — a brush face nearly parallel to the spine gives an
  ill-conditioned crossing parameter. Won't crash; document "cross edges
  cleanly."

### 7.5 Brushes do not need precision

They are selection volumes with slack designed in. Plain
`translate([25,25,10]) cylinder(r=8,h=4,center=true)` is fine. Anchor-precise
placement buys nothing where tolerance is already designed in.

---

## 8. Re-fillet protection

Because fillets are applied locally and therefore nest, every pass must avoid
re-filleting previous passes' output.

A fillet leaves behind two kinds of new concave crease:

- arc facet seams along the bead, ≈ `$fa`
- tangency creases at TA/TB, ≈ `$fa/2`

The §4.3 threshold rejects both by construction. But the case that actually
hurts is **chamfer-then-fillet**: a chamfer leaves genuine 45° creases, and a
later `r=2` fillet applies a full `t = 0.83 mm` fillet to each — on a bead only
~0.83 mm thick.

Equally bad: user has `r=1`, brushes the same edge with `r=3` expecting an
upgrade. The second pass sees only shallow creases, adds slivers, and the radius
**doesn't change**. Silent wrong answer.

### Mechanism: a reserved ID range, not a set

Do **not** keep a `std::set<uint32_t>` of issued IDs. Reserve one contiguous
block from Manifold's counter and make membership a range test.

```cpp
// Lazily, once per process. Function-local static avoids static-init order issues.
static uint32_t filletIdBase() {
    static const uint32_t base = manifold::Manifold::ReserveIDs(kFilletIdCount);
    return base;
}
constexpr uint32_t kFilletIdCount = 1u << 20;

inline bool isFilletId(uint32_t id) {
    return id - filletIdBase() < kFilletIdCount;   // unsigned wrap handles id < base
}
```

Each tool takes the next unused ID in the block. Test is O(1), state is one
`uint32_t`, and nothing grows.

**Critical detail:** the block must come from `ReserveIDs`, not be a hardcoded
range. Manifold hands out IDs sequentially from the same global counter for
every `AsOriginal()` and every `MeshGL` import, so a fixed range like `1..1000`
would collide with ordinary user geometry. Reserving 2^20 IDs is 0.02% of the
uint32 space and costs nothing but a counter bump.

If the block is exhausted, reserve another and keep a `std::vector` of ranges —
bounded by blocks, not by fillets.

#### Why the set was wrong, beyond the memory

The leak itself is modest — roughly 48 bytes per fillet per render. The real
problem is that it is **process-global mutable state that cannot be cleared.**

OpenSCAD rebuilds the tree on every F5, so the set grows with render count
rather than model complexity. The obvious fix — clear it at the start of each
render — is a **correctness bug**: OpenSCAD's geometry cache persists across
renders, so a cache hit from render 1 still carries render-1 IDs. Clear the set
and protection silently fails exactly on the cached paths, which is the worst
possible failure mode to debug.

A reserved range has no such tension. It is valid for the process lifetime,
cache hits included, and it stays correct if geometry evaluation is ever
parallelised.

(Note: the base differs per process, so this breaks if anyone ever adds a
*persistent* on-disk geometry cache. Currently in-memory only.)

### Stamping the tool

`Manifold::AsOriginal()` collapses the tool's build history (all those hulls)
into a single original — but it allocates its **own** ID, which won't be in our
block. To control the ID, go via `MeshGL`: export, set `runOriginalID` to our
reserved value, reimport. Verify the exact round-trip against current Manifold;
this is the one step most likely to have drifted.

### Named tags (layered on top, optional)

The range gives an automatic all-or-nothing exclusion. Named tags add
granularity without adding state, by deriving the ID deterministically:

```
id = filletIdBase() + (hash(name) % kFilletIdCount)
```

Pure function — no map, no allocation, stable across renders and cache hits.

```openscad
fillet_tool(r=2, tag="bore") thing();              // group it
fillet_tool(r=3, ignore_tags=["bore"]) other();    // re-fillet just that group
fillet_tool(r=3, ignore_tags=true) other();        // re-fillet everything
```

Keep automatic exclusion as the **default**. Nesting is the normal case (§3), so
protection cannot require the user to have remembered to tag.

**Verify before relying on hashed tags:** two calls sharing a tag name will
produce two distinct originals claiming the same `originalID`. We only ever
test membership, so it's fine for us, but check whether Manifold's own mesh
relation / property transfer assumes IDs are unique. If it does, fall back to
sequential allocation within the block plus a small `name → id` map — bounded by
distinct tag names in the script, which is a handful.

### What the test catches

One membership test kills both arc seams (both faces tool) and tangency creases
(one face tool, one original). Radius- and tessellation-independent.

Cached geometry carries its IDs in the mesh, so cache hits stay protected.

**Escape hatch:** chamfer-then-round is a legitimate technique. `ignore_tags`
should be documented, not hidden.

---

## 9. DO NOT DO THIS

Ordered roughly by how convincing the wrong answer looks.

### Geometry

**Do not hull thin discs or epsilon cylinders placed *at the tangency lines*.**
Those profiles contain no arc, so the hull is a flat slab — cross-section is the
straight chord `TA–TB`, giving a chamfer, not a fillet.

This is a caution about *which* profile, not about hulling. Hulling a profile
that contains the arc — the circular segment of §6.1 — is correct and is the
recommended construction. What remains true is that the fillet cross-section
itself is non-convex, so **no single hull is ever the tool**; it always takes a
difference of two convex families. That is the reason for W − U.

**Do not use rotation-minimizing frames** (or Frenet frames) along the spine.
The cross-section orientation is fully determined at every point by the two
adjacent surface normals. There is no twist freedom to resolve. RMF only matters
when the profile is free-floating.

**Do not compute ball centers per edge.** At a shared vertex the two adjacent
edges each produce their own center from their own facet normals; the centers
don't coincide, and you get a gap or a visible scallop. Compute per chain
vertex with averaged normals (§5.2). This is why chains exist.

**Do not resample the spine.** Use the mesh's own vertices (§5.4).

**Do not oversize W past the tangency lines** and expect U to trim it. Past
`TA` the ball has already curved away from face A, so `W − U` out there is a
tapering wedge of material sitting *on top of* the face. Visible bulge; looks
like a bad weld. Exact outward, sloppy inward.

**Do not just nudge `v` for the epsilon offset.** Displacing only the corner
gives a flank at `atan(eps/t)` — a couple of degrees — to face A. That's a
sliver along the entire chain. Use the pentagon (§6.2).

**Do not clip U.** Round caps scoop a dish out of W's end face (§7.2).

**Do not lerp normals and recompute** at clip parameters. Lerp the cross-section
points; that's what's consistent with the hull geometry you already committed to.

**Do not try to fix tangency slivers by shrinking `r`.** Grazing contact between
U and the original faces is inherent to filleting — the ball is tangent by
construction. Manifold's snapping handles it. Expect slivers; don't chase them.

**Do not hull the chains' end cross-sections in place of the corner sphere.**
It inscribes: ~0.18r deficit at the corner, two orders of magnitude worse than a
tessellated sphere, and it does not improve with finer arcs (§6.3.3).

**Do not use a triplet solution without the feasibility check** at a valence-4+
junction. It is tangent to its three faces and penetrates a fourth, gouging the
fillet back from that face (§6.3.1).

**Do not attempt unequal-radius vertex blends.** Equal-radius junctions of any
valence are tractable (§6.3.1); unequal breaks the constant-offset framing and
is genuinely hard. Detect shared chain endpoints between
differing-radius passes and **warn**. Defer.

### Classification

**Do not use `dot(nA, nB)` alone to determine inner vs outer.** It is blind to
the difference. Use the far-vertex test (§4.2).

**Do not use a relative / percentile dihedral classifier.** ("Take the N nearest
edges, compare φ to their p10.") It was considered and rejected — it's erratic
in three separate ways:

- Flat regions divide by zero (all neighbours 0°, ratio infinite, everything is
  a feature). Fixing that needs a floor, which is an absolute threshold — the
  thing you were avoiding, now hidden inside a statistic.
- Neighbourhoods near features are contaminated by those features. No good N.
- Neighbour-set membership changes discontinuously. Nudge a dimension by 0.1 mm,
  the mesh retriangulates, one edge's verdict flips, the chain splits, and the
  fillet develops a gap. The user sees a fillet that breaks when they change an
  unrelated parameter, with no way to reason about it.

Use the `$fa`-derived absolute threshold instead. It is a number the user can
see, print in a warning, and override.

**Do not hardcode a magic `φ_min`.** Derive from `$fa` / `$fn` (§4.3).

**Do not rely on originalID surviving everything.** `hull`, `minkowski`,
`projection`, CGAL fallbacks, and STL round-trips all lose provenance. Always
have the angle-threshold fallback.

**Do not invent your own ID numbering, and do not hardcode an ID range.**
Manifold's counter is global and monotonic; every `AsOriginal()` and every
`MeshGL` import draws from it. Reserve a block via `ReserveIDs` (§8).

**Do not keep a growing set of issued fillet IDs.** It grows with render count,
not model size, and it cannot be cleared between renders without breaking
protection on geometry cache hits — a silent failure on exactly the paths that
are hardest to debug. Use a range test (§8).

### API

**Do not build the fused multi-radius form** (`fillet(r=[2,5]) { thing();
brush_a(); brush_b(); }`). It was solving a problem that local application
doesn't have (§3).

**Do not make convexity a parameter.** The caller applies the boolean; a
mixed-convexity tool is unusable. Separate node names.

**Do not implement the geometry-modifying `fillet()` wrapper in C++.** It is six
lines of OpenSCAD over the tool nodes (§2.1). A fused node would share only the
edge-classification pass, which is a negligible fraction of runtime — the hulls
and the batch boolean dominate, and those can't be shared.

**Do not build an anchor/attachment system.** Out of scope; existing libraries
cover it, and brushes don't need one.

**Do not silently no-op on empty brush selection.** Warn with counts.

**Do not make `ignore_tags` impossible or undocumented.** Chamfer-then-round is
legitimate.

### Rejected architectures

These were evaluated at the start and ruled out. Don't drift back toward them.

**SDF / smooth-min (`smin`, implicit surfaces).** Cheap and general, but the
blend radius isn't the radius you asked for (smin bulges surfaces away from the
joint even far from the edge, and it isn't a rolling-ball blend), and `smin`
destroys the distance property so nesting compounds error. Also note the trap:
dilate is `f − r` and erode is `f + r`, so dilate-then-erode is algebraically a
no-op — nothing rounds unless you redistance between the steps. Wrong tool for
an exact-CSG kernel.

**Morphological closing via Minkowski** (`(S ⊕ B_r) ⊖ B_r`). Correct and
unusable. CGAL Minkowski on a tessellated sphere explodes combinatorially, and
it rounds *everything*, including features you wanted sharp.

**B-rep rolling-ball blends** (Parasolid/ACIS style: offset-surface spines,
trimmed faces, stitched canal patches). This is the "right" answer for a real
kernel, but OpenSCAD has no B-rep. Vertex blends alone would be most of the
work. Family D gets equal-radius corners free.

**Local SDF patch stitched into an exact mesh.** Interesting hybrid, much more
machinery (redistancing, dual contouring, boundary stitching) for no gain over
W − U in this codebase.

**Intersecting the brush with the finished tool volume** as the primary
mechanism. Cuts wherever the brush surface falls, including *along* the bead —
the bead is only `r(sec(φ/2) − 1)` ≈ `0.41r` thick at 90°, so a boundary 0.4 mm
off shaves the arc lengthwise and produces a fin with a crease, non-tangent to
the wall. Reads to the user as "wrong radius." Also prunes *after* building all
the hulls. Note the user can express clipping themselves in three lines
(`intersection() { fillet_tool(r=2) thing(); brush(); }`), so build the thing
that requires kernel internals.

---

## 10. Geometry instantiation: the structural cost

Any node that inspects a child's mesh forces that child to be evaluated. This is
the one genuinely structural objection to the whole approach, and it should be
answered deliberately rather than discovered in review.

### What is actually forced

**The `AbstractNode` tree survives.** Node structure and geometry evaluation are
separate in OpenSCAD; `.csg` export dumps the tree, so `fillet_tool(r=2) { ... }`
serialises fine and a future backend could treat it differently. Keep the node
lazy — record intent, evaluate only when a mesh is demanded — and this stays
true.

**What is forced is evaluation, at that point in the tree.** The child's CSG
subtree collapses to a concrete mesh, so:

- F5 preview pays full boolean evaluation instead of the cheap OpenCSG path.
  OpenCSG renders CSG trees per-pixel and never computes the boolean; it cannot
  shortcut a node that needs the result.
- Manifold's internal deferral is materialised at the same point.

`hull()` and `minkowski()` already do exactly this, so the precedent exists and
the behaviour is familiar. That is a defence of the approach, not of the cost.

### How `hull()` handles this — it doesn't

Worth knowing exactly, because it is the precedent and it sets the bar.

`CSGTreeEvaluator` builds the preview tree, and its leaves are `PolySet`s.
Union, difference and intersection stay as tree nodes for OpenCSG to resolve
per-pixel — that is why `difference()` is free in preview. But `hull()` cannot
be expressed as a union/difference/intersection of its children, so the
evaluator has no way to decompose it. It calls the geometry evaluator, computes
the hull for real, and inserts the result as **one opaque leaf**.

Computing that hull requires its children as concrete geometry, so any booleans
underneath are evaluated too. There is no shortcut and no laziness. Preview and
render do the same work.

So our node behaves identically to `hull()`, and that is simply accepted.

One thing `hull()` gets for free that we do not: its output is always convex, so
`convexity = 1` is correct and OpenCSG renders it properly. Our tool is a curved
bead — often a closed ring — where a ray crosses 2–4 surfaces. If the tool is
ever rendered in preview, the leaf needs a convexity hint or it will show holes
and wrong shading.

### The answer: preview passthrough, in one line of SCAD

Skip the fillet in `$preview` entirely. Because we return a *tool* rather than
modified geometry, this is trivially correct — the tool contributes nothing and
the base is untouched:

```openscad
module fillet_tool(r) { if (!$preview) _fillet_tool(r) children(); }
```

`union(){ thing(); fillet_tool(r) thing(); }` becomes just `thing()` in preview:
fully symbolic, native OpenCSG speed, and the node is never instantiated so its
children are never evaluated either. No C++ preview path is needed, and the
convexity problem above never arises.

A `filleted()` node returning modified geometry would have to reconstruct and
return its child; the tool form just disappears. This is the clearest payoff of
the tool-returning decision.

Give it an override for people who want to see fillets while iterating, and
document the default so nobody files "fillet does nothing in preview". One real
caveat worth a doc line: if a fillet adds material into a clearance gap, preview
will not show the interference.

### Rejected: pattern-matching the node tree

A tempting escape from (b): recognise `difference() { cube(); cylinder(); }` at
the *node* level, and emit `annulus_prism − torus` as a CSG subtree rather than
a mesh. No instantiation, tree fully preserved.

**Not doing this.** It bifurcates the implementation into a symbolic path and a
mesh path that must agree, adds a pattern-matcher over node types, and only ever
covers a handful of idioms. The complexity is not worth it for a subset of
inputs. Accept the wall.

(The same reasoning demotes the analytic fast paths in §11 from "primary path"
to what they were: an optional speed optimisation, to be taken only if profiling
demands it.)

### Precision, on the CGAL backend

Reading a mesh means reading floating-point coordinates. Under Manifold that
changes nothing, since it is already float-based. Under the CGAL path, geometry
carried as exact rationals is downgraded at the query point and cannot be
recovered. Worth a documented note rather than a fix.

### The honest concession

This is inherent to any mesh-query fillet: edges are a property of the evaluated
boundary, and there is no way to know where `cube − cylinder` seams without
computing the boolean. It is therefore the strongest argument for the implicit /
SDF route rejected in §9 — `union(r=3)` composes lazily precisely because it
never queries geometry.

That rejection stands on blend quality (the radius is not the radius asked for;
`smin` destroys the distance property so nesting compounds error), but the
tradeoff is real and should be stated plainly rather than argued away.

---

## 11. Performance

**Batch the unions.** A chain of K vertices gives K−1 hulls plus K spheres.
Folding left-to-right is effectively O(K²) because the accumulator keeps
growing. Use `Manifold::BatchBoolean` with a balanced tree. For a few hundred
pieces this is the difference between snappy and a coffee break.

**Share the analysis pass.** `fillet_tool` and `round_tool` on the same child do
identical edge classification and chaining, differing only in which chains they
keep and the sign of eps. Cache chain extraction keyed on the child's id string.

**OpenSCAD's geometry cache already dedupes repeated instantiation.**
`Tree::getIdString` is the dumped CSG text of the subtree with no node indices,
so `thing()` at three call sites hashes to the same key. Instances 2 and 3 cost
a tree walk and a string hash. Watch for: cache eviction (~100 MB default
budget), textual differences (`thing()` vs `thing($fn=64)`), and `rands()`
without a seed (genuinely different geometry each call — silently triples cost
and desyncs the tool from the base).

**Preview path — build this early.** `hull()` and `minkowski()` force full
geometry evaluation even in F5 preview; OpenCSG can't shortcut them, and this
node inherits that. Without a preview path every F5 pays the whole pipeline and
people stop using the operator. Options: drop sphere resolution hard, or skip
fillets below a size threshold.

**Analytic fast paths** (add early, high value):

- planar face ⊥ cylinder axis → emit an exact torus, skip the sweep entirely
- plane / plane → exact prism minus cylinder

These cover most real use.

**Cost profile of local application:** you pay analysis on a small mesh but
carry the denser result through downstream booleans. A `$fn=64` hole mouth at
`$fa=12` adds roughly 1–2k triangles; Manifold booleans are near-linear in face
count, so this is noise unless someone fillets everything.

---

## 12. Build order

1. **Edge classification + chain walking + debug visualization.** Render
   candidate chains coloured by accept/reject. Nothing downstream is debuggable
   without this.
2. **`chamfer_tool` / `bevel_tool`** — W only. Exact, cheap, no tangency, no
   `$fn` on the profile. Proves the whole scaffolding.
3. **`fillet_tool` / `round_tool`** — add U. Start with **disc** hulls (§6.1),
   two-face edges only, junctions left alone. This already covers hole mouths,
   single inside corners and closed chains, which is most of the value.
   Swap discs for circular segments once it works.
4. **Junction cells** (§6.3, §6.3.1). Separate milestone because it needs spine
   *and* wedge truncation plus a corner cell, none of which the two-face path
   exercises — but not optional. Every inside box corner is one. Build degree 3
   first, then generalise to the `Q`-vertex form; the code is the same shape.
5. **Preview passthrough** (§10). One line of SCAD, and it decides whether the
   operator is usable during iteration at all. Do not leave it to the end.
6. **Brush selection** — BVH, spine raycasting, interval clipping.
7. **Re-fillet tagging** — reserved ID range (§8).
8. **Analytic fast paths** — torus, prism. Optional; a speed optimisation only.
   Take it if profiling demands it, not by default (§10).
9. **The `fillet()` script wrapper** (§2.1) — trivial, but only meaningful once
   both signs work.

Deferred, in rough priority order:

- `runout=` — ramp `r` to zero over the last L of each interval. Nearly free:
  `C`, `TA`, `TB` are already computed per-vertex from a per-vertex radius, so
  the capsules just taper and the wedge narrows with them.
- Variable radius along a chain (same machinery).
- Anchor-driven auto-brush (`fillet(r=2, at="bore")`) — would need a geometry
  payload channel; revisit only if manual brush placement proves painful.
- Proper unequal-radius vertex blends.

---

## 13. Open questions to resolve during implementation

- Exact current Manifold signatures for `ReserveIDs`, `OriginalID`,
  `runOriginalID`, `BatchBoolean`, `Hull`.
- The `MeshGL` round-trip needed to stamp the tool with an ID from our reserved
  block, since `AsOriginal()` allocates its own (§8).
- Whether Manifold assumes `originalID` uniqueness anywhere internally — decides
  whether hashed tag names are viable (§8).
- Whether Manifold's `Hull` propagates vertex properties (output verts are a
  subset of input verts, so it plausibly could) — if so, vertex properties are a
  more granular alternative to face IDs for tagging.
- Branch vertices (degree > 2) in chain walking: split-and-warn is the plan,
  but check what real models actually produce.
- Closed chains: verify U and W both close cleanly with no seam at the wrap
  point.
- Whether `$fa`-derived `φ_min` behaves on `linear_extrude`/`rotate_extrude`
  output, where seam angles come from the profile rather than from
  `get_fragments_from_r`.

---

## 14. Upstream context and prior art

Checked July 2026. **No open PR implements this.** The feature has been
requested for 12 years and discussed seriously twice, but nobody is building it.

### Open issues

- **[#884](https://github.com/openscad/openscad/issues/884)** (2014, open, $80
  bounty) — "fillet/chamfer union operation." Narrower than this plan: it asks
  only for smoothing the joint between two unioned objects. Our boolean-seam
  filter (different `originalID`) is exactly that joint, so this plan subsumes
  the request.
- **[#3447](https://github.com/openscad/openscad/issues/3447)** (2020, open) —
  the substantial design thread. Two proposals, both unbuilt:
  1. Delegate to **Blender's BMesh** bevel (GPL-compatible), converting geometry
     in and out.
  2. Identify edges by a **search vector**: rotate the mesh so the vector is +X,
     sort vertices, walk edges in that order, filter by dihedral range, and let
     the user pick the *n*th matching edge.
- **[#3733](https://github.com/openscad/openscad/issues/3733)** (2021, open) —
  "edge shaping" / `router_extrude`: sweep an arbitrary profile along a 2D
  shape's edge. Adjacent; a generalised profile is a plausible later extension
  of `W`.
- **[#4617](https://github.com/openscad/openscad/issues/4617)** — someone
  building a morphological roundover out of `minkowski`, hitting
  **nondeterministic output** from parallel minkowski. Caution: verify
  `BatchBoolean` is deterministic before relying on it, and check whether
  `OPENSCAD_NO_PARALLEL=1` changes our results.

### Why now is different

The "CGAL can't do this" consensus predates
**[PR #4533](https://github.com/openscad/openscad/pull/4533)** (ochafik), which
brought Manifold in and made booleans and minkowski orders of magnitude faster.
Everything in this plan — cheap batched hulls, `originalID` provenance — depends
on that landing. Note that PR flagged Manifold as single-precision at the time;
we assume `MeshGL64`, so confirm the double-precision path.

### How this plan differs from #3447

- **Edge selection.** #3447's "*n*th matching edge along a search vector" is
  precisely the index-based scheme rejected in §7 — indices shuffle under
  parametric edits. Brushes are a different answer to the thread's own stated
  hardest problem, which the author named as reliably identifying the edge.
- **No new dependency.** BMesh means a half-edge topology layer and a modified
  mesh coming back. Returning a tool solid stays inside the CSG paradigm and
  needs nothing new. Expect "why not BMesh?" in review; that's the answer.
- Maintainer signal in the thread (t-paul) favoured **scoped options covering
  common cases** over a fully general solution. The tool-returning, brush-scoped
  design fits that; a general "fillet everything" node does not.

### User-space state of the art — read before starting

- **TLC123's `unionRound` / `unionRoundMask`** (linked from #884 and #3447). The
  closest existing thing: minkowski-based, with a user-supplied *mask* volume to
  isolate a locally-convex region — the same idea as our brush, arrived at
  independently. Known failures, both documented in #884 with pictures:
  three-way joints come out wrong, and concave-on-concave is slow and blemished.
  §6.3 handles equal-radius three-way corners correctly.
- **BOSL2** — `cuboid(rounding=)`, edge masks, `except=` edge exclusion. Mature
  for primitives, can't touch arbitrary boolean results.
- **Parkinbot's fillet library** (2025, Thingiverse 7240682) — ~40 lines,
  requires Manifold, no dependencies. Recent and worth reading.
- `ademuri/openscad-fillets`, `hraftery/prism-chamfer`,
  `SebiTimeWaster/Chamfers-for-OpenSCAD` — narrower, mostly extrusion-based.

### Engagement

No collision risk: #3447 has sat for six years with no code, and #884 for
twelve. Nobody is implementing either proposal, so there is nothing to
coordinate with and no reason to negotiate before building.

What to have ready when the work is shown, because both will be asked:

- **"Why not the *n*th-edge search vector?"** Because sorting makes enumeration
  deterministic for a fixed mesh; it does not make the *n*th edge the same
  physical edge after a parameter changes. Insert one feature and every index
  downstream in the sort order shifts. That is the topological naming problem,
  and ordering only makes the failure reproducible, not absent. Note the
  disagreement is narrow — the angle filter and chain walking in §4–5 are
  essentially #3447's steps; only the final "pick the *n*th" is replaced.
- **"Why not delegate to Blender's BMesh?"** It needs a half-edge topology layer
  and a new dependency, and it returns a modified mesh rather than composable
  CSG geometry. Returning a tool solid stays inside the existing paradigm.

TLC123's `unionRoundMask` fails on three-way joints (pictures in #884), which is
worth knowing as a correctness bar rather than a talking point: any complete
implementation has to handle inside box corners, and that is what §6.3 is for.

---

## 15. Test set

Each of these should be a regression case with a known-good result.

| case | checks |
|---|---|
| cube − cylinder, through hole | mouth fillet = `annulus_prism − torus` (§1) |
| cube − cylinder, blind hole | concave floor edge, closed chain |
| cube, outer edge | `round_tool`, convex sign flip |
| inside corner (floor + wall) | the worked example in §5.3 |
| inside box corner (3 faces) | trihedral: `P`, dual truncation, corner cell (§6.3) |
| symmetric square pyramid pocket | 4 offset planes concur; single sphere (§6.3.1) |
| asymmetric valence-4 junction | multiple feasible triplet vertices, hulled (§6.3.1) |
| valence-4 with one infeasible triplet | that solution discarded, no gouge (§6.3.1) |
| three faces with coplanar-parallel normals | singular solve rejected, not NaN (§6.3.2) |
| TLC123 three-cube cross | the documented failure case from #884 |
| same model at `$fn` = 8/16/64 | classification stable, no bore-seam fillets |
| fillet then fillet, larger r | second pass rejects, warns; no slivers |
| chamfer then fillet | rejected by tags; accepted with `ignore_tags` |
| brush covering half a chain | flat perpendicular cap, no scooped end |
| brush missing entirely | warning with counts, empty result |
| brush boundary on a spine vertex | stable across small parameter nudges |
| `r` larger than the face | warned and dropped, never clamped, never garbage |
| `r` past the limit by float noise only | clamped silently, fillet still built |
| imported STL | angle-threshold fallback path |
