# Mixed-sign corner saddle — L-bracket elbow defect

The concave-meets-convex corners (taxonomy defects ② and ⑤). Clearest on the
L-bracket elbow (`lbracket`, S3-T01): where the inner concave crease of the L
terminates at the front/back face and meets the two convex round-overs of that
face — one vertex, two convex edges + one concave edge.

Companion: `mixed-corner-saddle-sliver-guard.patch` (the `FilletBlend.cc` change,
also committed on this branch). **Not built or bench-rendered** — confirmed with a
standalone geometry harness; see "Verification".

---

## Symptoms (from the sheet shading)

At the elbow mixed corner:

1. **A bright needle** rising out of the corner — a near-zero-area sliver triangle
   standing proud.
2. **The concave valley pinches to a point** instead of fairing out into the two
   convex rounds; a dark notch sits at the vertex.
3. **Flat, creased facets** in the transition — piecewise-flat, not a smooth saddle.
4. Everything is pulled toward **one central point**.

Ruled out on the way in: the "left edge tapers / side face is a trapezoid" reading
is *not* a fillet bug. Measured round-over width down the vertical arm's straight
left edge is constant (~22 px in the tile; 31–38 only in the corner cap up top),
and it only widens in the last ~5 % at the elbow — which is this saddle. The
trapezoid is perspective (`sheet.sh` uses `--projection=p`); an orthographic render
(`--projection=o`) is the definitive check.

## Root cause

The mixed-sign patch (`emitSaddle`) has two faults, both in its tessellation, not
its geometry:

- **Centroid pole.** Every interior ring is fanned to a single centre vertex
  `Q = ring centroid` (`FilletBlend.cc` ~1210–1226). On a saddle the ring is
  strongly non-planar (deep concave side, shallow convex side), so `Q` sits off the
  surface and the fan facets don't follow the two-sign curvature — the pinch and the
  flat/creased look. Same pole pathology as the convex cap, but worse because a
  saddle has no single sphere.

- **Cusp needle.** A mixed corner keeps the arc *endpoints* in the ring (`lo = 0`
  for `mixedVerts`, unlike the convex case). Where the deep concave arc meets a
  shallow convex arc the two feet arrive with opposite tangents and nearly coincide;
  fanned inward to `Q` they give a razor-thin column — the bright needle. This is
  taxonomy ⑤ ("near-zero-area needle triangle") surfacing even though the saddle (②)
  is implemented.

## Fix in this patch — sliver guard (the needle)

Weld saddle vertices within `0.06 · radius` of each other, **never moving a
boundary (layer-0) vertex** — those are the exact ring points the strips weld to, so
the patch perimeter is untouched and only interior columns collapse. Collapsed
triangles go degenerate and `out.tri` drops them. This is the "degenerate-triangle
filter" the taxonomy called for, kept watertight by construction (the boundary edge
count is invariant under the weld).

It removes the needle. It **does not** remove the centroid pinch or the flat facets.

## Not fixed here — the pinch (follow-up)

The centroid-pole pinch and the piecewise-flat blend need a **pole-free saddle
re-tessellation**, analogous to the convex-cap fix but for two-sign curvature — e.g.
a Coons/transfinite patch across the ring, or a grid between the concave side and
the convex sides, sampled on the existing tangent-Bezier field so it still leaves
each boundary along the fillet tangent. Larger and higher-risk; deferred, not
attempted unverified.

## Verification

Done here (no toolchain):

- standalone harness building the mixed-corner ring (two convex arcs + one concave
  arc) and running `emitSaddle`'s exact topology (centroid `Q`, tangent Bezier `M`,
  concentric fan): reproduces the needle — worst triangle **aspect ≈ 75**, area
  ~200× below median;
- applying the guard's exact weld rule: aspect **≈ 75 → ≈ 5**, patch **boundary edge
  count unchanged** (still a closed perimeter that welds to the strips).

Still needed (on a build):

- `./sheet.sh S3-T01` (lbracket), `S3-T02` (box_step foot), `S3-T03` (rib end),
  `S3-T12` (refused_neighbour) — the mixed corners the needle shows on;
- a full `./sheet.sh` to confirm every tile stays VALID / manifold / `warnings=0`
  (the guard only welds and drops degenerates, so it must not open a hole — verify
  the mesh stays closed on the real geometry, not just the harness ring);
- `--projection=o` render of S3-T01 to close out the "trapezoid" as perspective.
