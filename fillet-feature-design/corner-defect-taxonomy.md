# Corner / junction defect taxonomy (B1 blend)

Diagnosis of the corner and junction artifacts marked in red on the bench contact
sheets (`fillet-bench/sheets/sheet-2.png`, `sheet-3.png`, `sheet-4.png`). Every
marked tile was re-rendered at the sheet's own camera (`--viewall --autocenter`,
Cornfield) and zoomed. All the meshes are VALID and manifold — these are quality
artifacts, not topological failures — so they are judged on the visual sheet, not
by `mesh.py`.

All code references are to `src/geometry/fillet/FilletBlend.cc`, in `emitCorner`
and the patches it dispatches to.

The marks resolve to **six causes**. Two are unimplemented features (the pinned
"final step" and the acknowledged open detection problem); two are bugs in
existing patches; two are working-but-crude.

| # | Cause | Marked tiles | Kind |
|---|---|---|---|
| ① | single-apex `cornerBall` fan | S2-T01/02 two_bosses corners, S2-T06 dome, S2-T08 hole_plate, S3-T10/11 rib_into_boss | implemented, crude |
| ② | flat ear-clip, no curved saddle | S3-T01 lbracket, S3-T02 box_step, S3-T03 rib end, S3-T12 refused_neighbour | **unimplemented** |
| ③ | concave `cornerBall` apex spike | S3-T05 pocket | bug |
| ④ | feature crowding / collision | S2-T01/02 two_bosses saddle | **unimplemented** |
| ⑤ | curved-wall junction slivers | S3-T09 mixed_fn | bug (hardest) |
| ⑥ | flat strip-end cap notch | S3-T07 brush_one_edge, S4-T01 shallow_crease | cosmetic |

---

## ① Convex box corners — crude single-apex cornerBall fan *(implemented, crude)*

**Where** three convex fillet edges meet at a box corner. Hits the single-sign
junction branch (`emitCorner`, ~L726): `cornerBall(u, concave=false)` seats one
apex, and the whole ring is fanned to that **single apex** (`out.tri(apex, …)`).

**Artifact** a faceted cone with radiating seams and a slight central pinch —
not a smooth spherical corner, because the corner is one point.

**Fix direction** tessellate the spherical cap: subdivide the ball patch (a
geodesic fan or a recursive triangle split against the sphere of radius `r`
centred at `cornerBall`) instead of a one-apex fan. Ring resolution already comes
from `arcSegs`; the cap should match it.

## ② Mixed-sign corners — flat ear-clip, no smooth saddle *(UNIMPLEMENTED)*

**Where** a concave crease and convex edges share one vertex (an L reflex corner,
a step foot, a rib end). Falls through the single-sign branch (`mixed == true`)
to `ringSaddle` (~L744, `bool ringSaddle` at L751): a **planar** ear-clip that
closes the ring watertight but flat.

**Artifact** a flat, creased triangular patch; where the concave crease dies into
a flat face it leaves a triangular V-notch (clearest on the rib end, S3-T03).

**Status** this is the deferred **mixed-sign saddle** — REQUIREMENTS.md gate (b),
"the mixed-sign SADDLE patch is the FINAL step," left planar-only on purpose. The
curved saddle blend is not built. Highest-value unimplemented feature; it is
vertex-atomic by design (one patch per mixed vertex, no per-edge-then-glue). See
`fillet-rewrite-direction` memory and REQUIREMENTS §gate(b).

## ③ Concave inner corners — cornerBall apex spike *(bug)*

**Where** an all-concave trihedral corner (pocket inner corners). Takes the
single-sign branch with `cornerBall(u, concave=true)`, which puts the ball centre
out in the open valley and adds an apex along `(centroid − C).normalized()`.

**Artifact** for a concave corner that apex direction can land so the fan throws a
thin sliver poking **out** above the rim (visible spike on S3-T05).

**Fix direction** the concave apex sign/placement is wrong for this configuration.
Either clamp the apex to the ring's own plane side, or (better) give the concave
corner the same tessellated spherical cap as ① but on the union side. Smallest
self-contained fix of the set.

## ④ Crowded / near-tangent features — colliding fillets spike *(UNIMPLEMENTED)*

**Where** two features sit within ~2r of each other (the two bosses). Their
concave base-fillets nearly collide in the narrow valley and the corner patches
at the pinch throw sharp flaps.

**Status** this is **crowding / neighbour refusal** — REQUIREMENTS §5a(c), called
"the one open detection problem," never built. There is no test that two blends
overlap, so the patches spike instead of the region being refused (promise 1) or
the two fillets being merged. Needs a detection pass: where a selected edge's
blend volume intersects another's, refuse-or-merge before emitting.

## ⑤ Junctions on a curved wall — needle slivers *(bug, hardest)*

**Where** straight filleted edges die into a round wall (box∩cylinder, S3-T09).
The strip-end / junction rings are strongly non-planar and `ringSaddle`'s ear-clip
produces spikes, flaps, and a near-zero-area **needle triangle**.

**Fix direction** related to ② but on curved geometry. The ear-clip needs a
sliver guard (reject ears below an area/aspect floor and re-triangulate), and the
ring points on the curved side should be projected consistently. Likely subsumed
by a real vertex-star saddle (②) plus a degenerate-triangle filter.

## ⑥ Fillet strip ends — flat perpendicular cap notch *(cosmetic)*

**Where** a strip ends at a brush boundary, a sub-threshold crease, or a corner
(brush_one_edge ends, shallow_crease corners). The strip-end path caps it with a
flat perpendicular ear-clip (`ringSaddle` / `out.fan`), working as designed.

**Artifact** a small triangular facet where a rounded profile meets a sharp edge.

**Fix direction** round the cap — sweep the cross-section's arc through a
quarter-disk to the end face instead of a flat ear — if a rounded strip end is
wanted. Low priority.

---

## Suggested order for the next agent

1. **③ pocket concave-apex spike** — smallest, self-contained; good warm-up that
   also informs ①.
2. **① convex corner sphere** — replace the one-apex fan with a tessellated cap;
   visible on the most tiles.
3. **② mixed-sign curved saddle** — the pinned final feature; largest. ⑤ likely
   falls out of a correct vertex-star saddle plus a sliver guard.
4. **④ crowding / neighbour refusal** — the open detection problem; do last, it is
   orthogonal to the patch work above.

Reproduce any tile with `fillet-bench/sheet.sh S<n>-T<nn>`; the id → model/args
map is in `fillet-bench/sheets/INDEX.md`.
