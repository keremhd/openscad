# Tangent-junction fillet — the merged-surface taper (next session)

**Status: diagnosed and prototyped 2026-08-08, deferred to its own session.** The centerline
taper on `tee`/`tee_oblique`/`cross` — two cylinders meeting — is fully understood and a fix
direction is chosen. Completing it needs the kept-sharp-edge closure (step 4 / partial selection),
so it is scoped as a milestone, not a patch. Read this before touching `smoothSurfaces` or the
cross-section path in `FilletBlend.cc`.

## The symptom

On the `tee`, the horizontal cylinder looks **tapered** into the junction: along its top
centre-line the surface ramps from the junction out to the free end instead of holding a clean
fillet that ends ~R from the crease. Measured (r=1): the concave cove correctly bulges to r≈5.98 at
the crease, but there is **no tangent-cap vertex** between the junction and the far ring, so the
bulge is smeared linearly across the whole wall facet. On a clean concave crease (`boss_plate`) the
same code caps correctly at foot+R — so this is specific to the junction.

## Root cause — surface merging across the tangent gap (non-local)

The two-cylinder intersection curve is a loop whose dihedral **varies**: ~87.8° at top/bottom,
~15.86° at the tangent sides. The feature threshold is 46°.

`smoothSurfaces` groups triangles into a "surface" by walking every edge **below** 46°. The
15.86° tangent-side edges are below 46°, so the walk **crosses the tangent gap and fuses the
horizontal wall and the vertical wall into one surface.** After that, *every* crease edge on the
junction — including the 90° top edge — has `surfaceOf[t0] == surfaceOf[t1]` (`SA == SB`), and the
fillet cross-section (`crossSectionAt` → `insetPoint(u,SA)`, `insetPoint(u,SB)`) collapses: both
tangent points land on the same point, the arc degenerates, and the wall is never capped. Confirmed
directly by instrumenting `emitEdge`: at the top edge, `SA=0 SB=0`, `Ta == Tb`.

So the tangent edge corrupts the top edge **through the surface grouping** — exactly the
non-locality the shape suggested. It cannot be fixed by tuning the threshold: the tangent crease
(15.86°) is *shallower* than the cylinder's own facet angle (22.5° at fn=16), so no single angle
both keeps a cylinder wall unified and splits the two walls apart.

## The chosen fix (owner, 2026-08-08): stop the surface at the crease

When surface grouping follows sub-threshold edges to merge, it must **stop at the intersection
curve** — treat the tangent-gap edges as a boundary, "as if there were a brush edge there," so the
two walls stay distinct surfaces. Then `SA ≠ SB` on every crease edge, the arc builds correctly,
and the tangent gap becomes a **kept-sharp (unfilleted) boundary** — which is the desired result
(the tangent sides are not meant to be filleted; the 46° restriction is fine).

Preferred over the fallback ("merge, then refuse to fillet any `SA==SB` edge"), which would skip
the whole junction instead of filleting the good part.

Two ways to identify where to stop the surface, both prototyped:

1. **Provenance** — stop grouping across an edge whose two faces carry different Manifold source
   ids (`Tri::originalID`). The intersection curve is exactly where the two source cylinders meet.
   Cheap and exact for boolean-built input (the operator unions its children, so provenance is
   present). Caveat: not mesh-only — a re-imported STL with one id would not split. Weigh against
   the "classification is a property of the mesh" principle (though this is internal surface
   grouping, not crease *selection*, and the tangent edges still stay unfilleted either way).
2. **Sector-local sides (mesh-only)** — do not rely on a global surface at all; derive each
   fillet's two sides from the crease edge's own incident triangles via the fan sectors around each
   vertex. Correct even on a fold, provenance-free. This is the fuller rewrite.

## Why it is a milestone, not a patch — the real blocker

**Both** methods were implemented and **both produce the identical result**: the centre-line arc is
fixed and closes, but the junction now leaves **16 open edges at the tangent sides** (fn=16), where
the filleted fragments end against the now-sharp tangent gap. The blend refuses (valid-but-taper →
unfilleted), a regression, unless those ends are closed.

Closing them is the **kept-sharp-edge seam** case — step 4 (partial selection): where a blended
surface borders a kept-sharp edge, the manifold needs per-vertex splitting so the strip end sews to
the sharp boundary. The tangent-gap edges are precisely "feature-but-not-selected" kept edges once
we stop the surface there. So this fix **lands on top of step 4**, and its boundary is a branching
graph a crude centroid-fan does not converge on (tried: 16→8→12, never 0).

## Build order for the next session

1. Land or advance **step 4 (partial selection / kept-sharp-edge closure)** first — it is the
   machinery this needs, and `START-HERE` already lists it as the main remaining work.
2. Add the surface-stop: extend the feature/boundary set with the tangent-gap edges (provenance or
   sector-local), so `SA ≠ SB` on the junction.
3. Close the fragment ends via the step-4 kept-edge seam, not a crude fan.
4. Verify: `tee` top-generator profile caps at ~junction+R (r stays 5 past the cap), full
   `fillet-bench` stays **353/353 VALID** and deterministic, and the junction sheet looks right.

## Artifacts

- `tangent-junction-sector-prototype.patch` — the mesh-only sector-local prototype (fan sectors,
  symmetric arc centre, `capHoles`), stashed off `kerem-fillet`. It fixes the centre-line and shows
  the 16-open-edge blocker; apply with `git apply` to resume. Not committed — incomplete closure.
- The provenance-split variant is a ~25-line change to `buildOn`'s surface derivation (see this
  note's history / re-derive from §"chosen fix" 1); it gives the same 16-edge blocker.

## What is NOT this bug

The **straight-crease taper** (lbracket/box_step/rib), a separate and general bug, is **fixed and
committed** (`along-sweep-stations.md`, the station-split). This note is only the curved
tangent-junction case.
