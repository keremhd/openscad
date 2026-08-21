# Along-sweep station density — the second density floor

**Status: specified 2026-08-08, for implementation on `kerem-fillet`.** Read
`REQUIREMENTS.md` first; this note fills in one floor it names but never pinned down for the B1
(topological-bevel) architecture.

## The bug this closes

A fillet strip in B1 (`FilletBlend.cc`) places a cross-section **only at the existing mesh
vertices** along a crease — `emitEdge` builds one quad strip between `crossSectionAt(e.first)` and
`crossSectionAt(e.second)`, the two endpoints of a *mesh* edge, with nothing in between. So the
number of cross-sections ("stations") along a fillet equals the number of mesh vertices on that
crease.

On a **straight** crease that is a single mesh edge (e.g. every top edge of `lbracket`'s plates)
that means **two** stations, both at corners. At a corner `insetPoint` mitres several feature edges
and pulls the tangent point inward by ~setback. The strip then linearly interpolates between two
inward-pulled end profiles, so a fillet that should be a constant-profile prism bulges/tapers along
its length. This is the visible defect: a long straight fillet that is fat in the middle and
pinched at the ends.

This is **not** about curved-crease smoothness. On a cylinder rim the stations already equal the
facet count, and that is correct and intended: **a fillet is no smoother than the surface it
blends, and the mesh is the surface.** We do not recover or estimate any analytic curve. Raising
`$fn` on the model is the only — and expected — way to get a rounder fillet on a curved crease.

## What the spec already said, and what was missing

`REQUIREMENTS.md` §9a names **two density floors**: "a `$fn`-like cap on station spacing *along the
sweep*; `arcSegments` *across the section*." The redesign note (`archive/2026-08-06-…`, §3e/§4)
describes the along-sweep floor as "a `$fn`-like parameter [that] caps the maximum step
independently … symmetric with `arcSegments`."

- **Across-section floor — implemented.** `Blender::arcSegs`, set from the discretizer in
  `buildBlend`.
- **Along-sweep floor — never implemented.** The spec for it lived inside the *old* swept-tool
  contact-search machinery, which "walked the wall surface" and could sample as densely as it
  liked. B1 discarded that and reads mesh vertices directly, inheriting the mesh's resolution with
  **no floor and no way to add one**. How a topological bevel adds along-sweep detail when the mesh
  crease is coarser than the fillet's own feature size was left undesigned.

This note is that design.

## The mechanism: split long crease edges (geometry-exact)

Refine the mesh **before** the blend runs: any selected feature edge longer than a cap `L` is split
into `ceil(len/L)` equal pieces, and the two triangles incident to it are re-triangulated to keep
the mesh manifold.

Two properties make this correct and cheap:

- **Geometry-exact — the input solid does not change.** The new vertices lie *on* the edge segment,
  and each bisected triangle is coplanar with the original it came from. The surface is the same
  surface, with more vertices on the crease. No vertex moves off the solid; dihedral angles are
  unchanged, so classification, concavity and surface grouping are identical on the refined mesh.
- **Manifold-safe — no T-junctions.** Because we split the two incident triangles too (not just the
  strip), each new station is a real shared vertex. The inset triangle (surface re-emission) and
  the fillet strip both reference the same `inset(P)`, so the seam closes and `OutMesh::orient()`
  reports no open edges. (Adding stations to the strip alone would T-junction against the un-split
  inset triangle and be refused as a hole.)

And it needs **no change to the blend logic**. An interior station `P` on a straight crease has two
collinear feature sub-edges `(a,P)` and `(P,b)`; `insetPoint(P)` mitres two parallel offset lines
and falls back to the clean perpendicular inset, and `crossSectionAt(P)` averages a single surface
normal into a clean arc. The middle of a straight run becomes a true prism; the residual taper is
confined to the one short end-span next to each corner.

Curved creases get split too (their edges may exceed `L`); the new stations land on the chord and
"round the same" — one extra facet subdividing a chord, no better and no worse than the mesh
already was. Harmless, per the axiom above.

### Placement in the pipeline

```
m   = mergeMesh(...)
adj = buildEdgeAdjacency(m.tris)
threshold = ...
--- NEW: subdivide selected feature edges with len > L, rebuild adj ---
surfaceOf = smoothSurfaces(m, adj, threshold)   // on the refined mesh
... classify / select / blend, unchanged ...
```

Only **selected** feature edges are split (the ones actually blended); kept-sharp and non-feature
edges are left alone, so flat faces are never needlessly tessellated. Because splitting produces
only sub-edges ≤ `L` and 0-dihedral interior edges (never a new long feature), the split list taken
once from the original mesh is complete — no iteration to a fixpoint is required.

## The cap `L`, and the knob

`L = k · r`, where `r` is the fillet radius / chamfer setback (`node.size`) and `k` is a quality
multiplier.

Why tie it to `r` and **not** to `arcSegs`/the discretizer: `arcSegs` is scaled to the (small)
fillet radius, so an "isotropic" cap (`L` = arc-chord) forces ~50 stations onto a 40 mm model edge
with `r = 2` — the "too many points in edge cases" we explicitly want to avoid. The taper is a
*corner* artifact: interior stations are taper-free, so we only need enough of them to break the
corner-to-corner lerp and keep each end-span transition ≲ a few `r`. `L = k·r` does exactly that,
and the station count scales with `len/(k·r)`, not with the fillet's facet size.

Rough counts on `lbracket`'s 40 mm edge, `r = 2`:

| k | L | stations on a 40 mm edge |
|---|-----|--------------------------|
| 2 | 4 mm  | ~10 |
| 4 | 8 mm  | ~5  |
| 8 | 16 mm | ~2–3 |

Even `k = 8` removes the visible taper. **Default `k = 4`.**

Guards (from the redesign note's termination rules, carried here):

- **Minimum span length** — never split a sub-edge below a small absolute floor, so numerical
  degeneracy and vertex explosion on already-fine meshes are impossible.
- **Maximum stations per edge** — a backstop cap on inserts per edge.

### Configurable later

`k` (equivalently `L`) is **hardcoded to the default for now**. It is the along-sweep counterpart
of the across-section discretizer and belongs, eventually, to the same `$fn`/`$fa`/`$fs` family the
operator already reads through `CurveDiscretizer`.

`L` is a **maximum segment length along the crease**, which is conceptually the `$fs` ("fragment
size / minimum segment length") axis rather than `$fn` (fixed count) or `$fa` (angle). So the
natural future exposure is a length-style control — a dedicated fillet argument or a new special
variable (working name `$fillet_fs`, or folding it into how the operator reads `$fs`) — **not** a
direct reuse of the model's `$fs`, whose small default would over-split long model edges the same
way the isotropic cap does. Until a name is chosen, `k = 4` stays a constant in `buildBlend`.

## Safety net: fall back when subdivision would break a solid

Adding stations is safe on well-behaved geometry, but it can push a *marginal*
case over the edge. Where the fillet is over-size for a feature — two fillets
meeting along an **exact tangency line** (`thin_slab` r=2 on a 4 mm plate:
setback 2+2 = 4 = full thickness; `rib` r=1 on its 2 mm bottom tab: 1+1 = 2) —
the collapsed region is a measure-zero degeneracy the un-subdivided build closes
by luck (its degenerate strip triangles weld away). Densifying that line with
extra stations turns it into a genuine non-manifold *fin* (an edge shared by four
triangles). Both defaults and every non-tangent radius stay valid; only the exact
knife-edge regresses.

So the blend is built **twice at most**: once on the subdivided mesh, and — only
if that result is non-manifold (an open edge, or an edge used by >2 triangles) —
again on the raw mesh. The raw build is exactly the pre-floor behaviour, so
**subdivision can never make a cell worse than it was before this floor existed.**
The over-size collision itself is the deferred crowding/over-size regime
(`REQUIREMENTS.md` §5a c, §9a r~R); this note does not try to solve it, only to
guarantee the station floor never regresses it. The second build runs only in
that rare degenerate case.

## Scope — what this does and does not fix

- **Fixes:** Mode 2, the straight-crease taper — `lbracket` and any long straight fillet or chamfer.
- **Does not fix:** the `tee` concave-junction flat ramp seen even at `$fn = 64`. That crease
  already has ample stations; the flat comes from the dihedral → 180° near the tangent top/bottom
  of two crossing cylinders, where `setback = r·tan(θ/2)` blows up and the arc degenerates. That is
  a **setback/tangency** problem, tracked separately (and it overlaps the deferred mixed-sign
  saddle). Edge-splitting neither helps nor hurts it.
