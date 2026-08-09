# Mixed-corner saddle — state and next steps

Handoff for the next agent working on the B1 fillet's **mixed-sign corner** patch
(a concave crease + convex edges meeting at one vertex: an L reflex, a box step, a
rib end). All code is in `src/geometry/fillet/FilletBlend.cc`.

## What is DONE and committed (branch `kerem-fillet`)

1. **The blade / fin is fixed — pull-in seating.** Root cause: at a mixed corner
   each selected edge's end cross-section had its two feet at each face's *own* mitre
   station — two different arc-lengths along the edge — so the first strip quad
   twisted into a thin blade. Fix: seat both feet at ONE common pull-in station (the
   furthest mitre) → a clean perpendicular slice. See `pullStation`, `stripFoot`,
   `crossSectionEdge`, `emitSurfaceTri`/`fillPlanarPolygon` (re-triangulates the
   shorter-mitre face so the pulled foot is a shared vertex, else a T-junction/hole).
   Gated to `mixedVerts`; a per-model fallback (`buildOn(..., pullIn)`) re-runs with
   baseline seating if the pull-in can't weld, so nothing regresses.

2. **The saddle asymmetry is fixed — symmetric fill.** The old ear-clip triangulated
   a mirror-symmetric ring asymmetrically. Now `emitSaddle` builds a **concentric-ring
   mesh** (k layers boundary→centre, k scales with `arcSegs`/$fn), which is symmetric
   and — unlike the earlier centroid fan — tessellates evenly (no long coarse
   boundary triangles), so the corner keeps pace with smooth high-$fn strips.

Validation bar (must hold after any change): the full `fillet-bench` sweep is
**353/353 valid, 0 fallback-to-unblended, 0 topology diff vs the prior baseline**
(only v/e/f change, on mixed-corner models); unit suite **121/121**.

## What REMAINS — the saddle still caves

The `emitSaddle` fill is a **Laplacian membrane = a minimal (soap-film) surface**.
Measured on the lbracket reflex corner (r=2): the through-edge valley bottom is pulled
to ≈(6.25, ·, 6.25), ρ≈2.48 from the reflex axis (8,·,8) vs the ideal ρ=2.0 — it
**caves toward the reentrant corner** instead of holding the fillet radius outward.
Owner-confirmed by eye ("the fillet edge that comes through the solid goes into the
solid; it should be more towards the outside"). The concentric-ring change makes it
*smooth*, not *bulged* — the caving is unchanged.

## The recommended fix — an analytic tangent-continuous patch (Gregory/Bézier)

This is the owner's preferred direction and the right tool: it fixes BOTH the residual
caving AND is evaluable at any resolution.

- **Why it works.** Caving is exactly what area-minimization does. A patch that instead
  **preserves the boundary tangents** (G1 to the incident fillet strips) leaves each
  ring point in the direction the roll was already heading, so it holds the radius out.
  A free-form patch also carries a *saddle* (negative curvature) natively — unlike a
  sphere, which fills the valley.
- **Shape.** N-sided **Gregory patch** over the ring (this is what CAD kernels use for
  setback vertex blends). The ring is N-sided *with flat mitre connectors* between the
  arcs — handle those connector segments as boundary pieces with a flat cross-boundary
  tangent (the face normal). Tessellate the patch as finely as `arcSegs`.
- **Data you already have.** Each ring point's fillet-surface normal is cheap:
  `filletCenter(u,t0,t1,concave)` (add it back — it was prototyped then reverted) gives
  the arc centre C, and an arc point p's normal is simply `(p − C)`. Connector points'
  normal is the incident face normal `m.tris[tri].normal`. Build a `ringNrm` parallel
  to `ring` in `emitCorner` (a `pushRing` lambda keeps it in sync with the dedup).

## What was TRIED and REJECTED this session (do not repeat)

- **Project the membrane onto the edge fillet cylinders** (nearest, and inverse-square
  weighted): radiates **spikes** from the fan centre — projection direction flips
  around the apex. Dead end for any "deform a fan onto primitives" scheme.
- **Fit one sphere to the ring and project**: bulges out correctly but **fills the
  concave valley** (a sphere is convex everywhere). Confirms a single primitive can't
  do a saddle.
- **C0 biharmonic (thin-plate) solve, boundary fixed**: overshoots at the free
  boundary (points poke out the front face); centre apex unchanged. Needs a C1 BC.
- **Tangent collar (fix an inner ring stepped along the fillet tangent) + Laplacian
  centre**: marginal — apex moved only ρ 2.475→2.487, y 0.90→0.79 — because the saddle
  centre is a genuine transition point fixed largely by the ring; a linear tangent step
  can't override it. A *discrete* membrane is the wrong tool; you need the analytic patch.
- **Scaling the centroid-fan's `kSplitRounds` with $fn**: no visible change — central
  subdivision doesn't refine the fan's long boundary triangles. (Replaced by the
  concentric-ring mesh, which does.)

## Build / test / sweep

- Build: `CCACHE_BASEDIR=$PWD make -C build OpenSCADExe -j8`
- One model: `build/OpenSCAD.app/Contents/MacOS/OpenSCAD --enable=fillet --backend=manifold --render -o out.stl fillet-bench/models/lbracket.scad`; validate `python3 fillet-bench/mesh.py out.stl`.
- Corner render (edges): add `--view=edges --camera=6,2,6,62,0,35,20 --imgsize=700,700` (lbracket reflex corner). Force `LC_ALL=C` if you print coordinates — the shell locale uses decimal commas.
- Unit: `build/OpenSCADUnitTests` (Catch2, 121 assertions).
- Sweep: `cd fillet-bench && BIN=<pinned-copy> FLAGS=--enable=fillet ./sweep.sh --fresh`.
  **Pin a copy of the binary** and point BIN at it — do not let a rebuild replace the
  binary mid-sweep. Compare topology columns (valid/nonman/chi/genus/comp/warn) against
  the committed `results/sweep.tsv`; geometry (v/e/f) is expected to differ on
  mixed-corner models. hole_plate is legitimately genus 1; shallow_crease is 2 components.

The mixed corners on the bench are all **1 concave + N convex** (lbracket, box_step,
rib, two_bosses, pocket, rib_into_boss, thin_slab, mixed_fn). Two-or-more concave
edges meeting at one vertex would recurse the saddle problem and is out of scope until
a case appears.
