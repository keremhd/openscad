# Theory: the L-bracket mixed-corner saddle

A theory of *why* the concave-meets-convex corner (the L-bracket elbow, S3-T01)
looks wrong, and what a real fix would have to do. Companion to
`mixed-corner-saddle.md` (which records the shipped sliver guard). This one is the
causal theory and the solution space — read it if you want to write the real fix.

## The corner

At the elbow, one vertex carries three selected edges: two **convex** round-overs
(the front-face edges) and one **concave** crease (the inner corner of the L). No
single ball caps a two-sign vertex, so `emitCorner` sends it to `emitSaddle`, which
fills the vertex ring with a membrane.

## What `emitSaddle` builds

For the ring `B[i]` it computes a centroid `Q`, a per-vertex tangent control point
`M[i]` (the inward chord projected off the fillet-surface normal, so each radial
curve leaves the boundary along the fillet tangent — G1 to the strips), and sweeps
`k` concentric layers along the quadratic Bezier `B[i] → M[i] → Q`, **all layers
ending at the one vertex `Q`**, closed by a fan.

The tangent field is the good idea here — it is what makes the patch continue the
roll instead of caving to a minimal surface. The failure is topological, downstream
of it.

## Theory of the two artifacts

**1. The pinch (and the flat, creased wedges).** `Q` is a *topological pole*: a
single vertex of valence `n` that every interior layer collapses onto. Two
consequences:

- A saddle ring is strongly non-planar — deep on the concave side, shallow on the
  convex side. The flat centroid `Q` sits *off* the surface the tangents are
  heading for, so the fan folds toward an off-surface point. That is the caved,
  creased look.
- Even if `Q` were perfectly placed, a single apex cannot carry two-sign curvature:
  the surface has to be convex along one boundary direction and concave along the
  other through the same centre. One vertex fanned to the whole ring can only
  average them, so the blend reads as faceted rather than saddle-shaped.

This is the *same* pole pathology as the convex cap — but the convex cap had a
sphere to sit every vertex on, so replacing the fan with a barycentric subdivision
fixed it. A saddle has **no single surface** to project onto, so the same trick does
not port over. Confirmed cheaply: moving `Q` from the ring centroid to the mean of
the tangent controls `M[]` shifts the pole by ~0.02·r and changes nothing
qualitatively — you cannot relocate a pole out of existence.

**2. The needle.** A mixed corner keeps the arc *endpoints* in the ring (`lo = 0`
for `mixedVerts`, unlike the single-sign cap). Where the deep concave arc meets a
shallow convex arc, the two feet nearly coincide but arrive with *opposite*
tangents. Fanned inward to `Q` that pair spans a razor-thin column — the bright
needle (taxonomy ⑤). This one *is* local and cheap to kill (see the guard), because
it is a degenerate-triangle problem, not a surface problem.

## What a real fix has to satisfy

A correct mixed-corner patch must:

1. interpolate the boundary ring exactly (weld to the strips);
2. leave each boundary point along its fillet tangent (G1 — keep the `M[i]` field);
3. carry **opposite-sign curvature** across the patch (genuine saddle);
4. have **no interior pole** (no single high-valence collapse vertex).

The current patch gets 1 and 2 and fails 3 and 4 together, because 4 (the pole)
forces 3 to average out.

## Solution space (in rough order of effort)

- **A. Discrete transfinite / Coons patch.** Split the ring into opposite sides —
  the concave arc opposite the convex-convex run, connectors as the other two sides —
  and fill with a Coons blend of the four boundary curves. Interior points are
  bilinear blends of opposite boundaries, so there is no pole and the two sides'
  curvatures are carried independently. Hardest part is a *robust 4-side partition*
  of an arbitrary mixed ring (how many convex edges, where the connectors fall).

- **B. Interior vertices on the tangent field (no centroid).** Keep the concentric
  topology but stop collapsing: place each inner layer's vertices by blending the
  two *adjacent* boundary tangent curves (mean-value / Laplacian-with-tangents on a
  fixed inner grid) instead of all diving to one `Q`. The innermost layer is a small
  polygon triangulated on its own, not a fan to an apex. Removes pole (4), keeps the
  `M[i]` tangents (2), approximates (3). Least invasive to the existing code.

- **C. Approximate interim (shipped).** The sliver guard: weld near-coincident
  interior vertices so the needle collapses, boundary untouched. Removes the sharp
  artifact (4-needle) but not the pinch. This is what is committed; A or B is the
  real answer.

## An approximate solution worth trying first (B, sketched)

Cheapest step toward pole-free without a full Coons implementation:

1. Build the same `B[i]`, `M[i]`.
2. For inner layers `l = 1 … k-1`, instead of `b0·B + b1·M + b2·Q`, set each inner
   vertex to the blend of its own radial Bezier evaluated at `s(l)` **and** the two
   circumferential neighbours' radial Beziers — i.e. one or two Jacobi/Laplacian
   smoothing passes over the inner grid with the boundary and the `M` ring pinned.
   The centre is then a *small ring*, not a point.
3. Triangulate the innermost small ring by ear-clip (`earClipRing` already exists),
   not a fan to one apex.

This keeps 1, 2, 4 and approximates 3, and reuses machinery already in the file. It
is still an approximation (a relaxed grid, not an exact saddle), and it is **not
written or tested** — offered as the first thing to try.

## Caveat

Everything here is reasoning plus a standalone harness that reproduces the ring and
runs `emitSaddle`'s exact topology (needle at aspect ≈ 75; pole invariant under
recentering). None of it is built against the real mesh. The real test is a bench
render of S3-T01 and the manifold check.
