# Fillet Operator — Detailed Milestone Split

Companion to `fillet-operator-plan.md`. That document is the design; this one is
the delivery plan: small, individually mergeable milestones, plus the
visual-inspection harness used to steer and verify the work.

Each milestone below is intended to be **one compilable, mergeable PR**.

---

## Feasibility notes (from a source dive)

Confirmed against the current tree; these shape or correct the plan.

- **Node pattern is a straight clone of `CgalAdvNode`** (`src/core/CgalAdvNode.{h,cc}`):
  a node class with a type enum + `VISITABLE()`, one free `builtin_*` factory
  function per name, registered via `register_builtin_*()` wired into
  `src/core/Builtins.cc`. The four tool names (`fillet_tool`, `round_tool`,
  `chamfer_tool`, `bevel_tool`) are four factories over one node class + enum.
- **`$fn/$fa/$fs` are not readable in the evaluator.** There is no
  `get_fragments_from_r` anymore. Nodes carry a `CurveDiscretizer`
  (`src/core/CurveDiscretizer.h`), built in the factory from the `Parameters`
  (as `primitives.cc` does), and call `getCircularSegmentCount(r, angle)` at
  geometry-build time. **The plan's §4.3 formula must be expressed through
  `CurveDiscretizer`, not a raw `$fa` read.**
- **Mesh access** is `getManifold().GetMeshGL64()`, then walk `vertProperties`
  (stride `numProp`, xyz first), `triVerts`, `runIndex`, `runOriginalID`. The
  reference implementation is `ManifoldGeometry::toPolySet()`
  (`src/geometry/manifold/ManifoldGeometry.cc:129-210`).
- **Halfedge adjacency is not handed to you.** MeshGL gives triangles, not the
  edge→2-face pairing. "Every edge has two faces" (§4.1) holds in the manifold,
  but you must **rebuild** adjacency from `triVerts` (merge per-run duplicated
  vertices by position first, then match undirected vertex pairs). This is real
  work and gets its own milestone (M2).
- **All required Manifold APIs exist** (submodule v3.5.1): `Hull`,
  `BatchBoolean`, `ReserveIDs`, `AsOriginal`, `OriginalID`, `MeshGL64`,
  `runOriginalID`. **`BatchBoolean` has zero existing callers in the repo** — we
  are first; verify determinism (cf. plan §14 / issue #4617) before relying on
  it.
- **The MeshGL ID round-trip for tagging (§8) is the single riskiest step** and
  is scheduled late (M11).
- **Debug viz is cheap:** emit a `ManifoldGeometry`/`PolySet` with `setColor`
  per region; color survives to render via `originalID`.
- **Tests auto-glob:** any `.scad` dropped in `tests/data/scad/3D/features/` is
  picked up by CTest (dump/render/preview/throwntogether). Baseline PNGs are
  committed per feature under `tests/regression/<dir>/`.

---

## Milestone overview

| # | Milestone | Type | Plan §
|---|---|---|---|
| **M0** | Node class + 4 builtins registered, parse params, `visit()` returns empty (no-op tool) | plumbing | §2 |
| **M1** | Visual test harness — hand-written reference tools + cross-cut viewer | viz/test | §15 |
| **M2** | Mesh extraction + edge→2-face adjacency + classification | plumbing | §4 |
| **M3** | Debug visualization of classified edges | debug viz | §12.1 |
| **M4** | Chain walking + per-vertex normals, `C`/`TA`/`TB`, debug spine | debug viz | §5 |
| **M5** | `chamfer_tool`/`bevel_tool` — W only (first real geometry) | geometry | §6.2, §6.4 |
| **M6** | Automated regression: turn M1 harness files into ctest baselines | test | §15 |
| **M7** | `fillet_tool`/`round_tool` — disc hulls, then swap to circular segments | geometry | §6.1 |
| **M8** | Junction cells degree 3 (`P`, dual truncation, corner cell) | geometry | §6.3 |
| **M9** | Junction degree ≥4 (Q-vertex) + numerical guards + runout fallback | geometry | §6.3.1-6.3.2 |
| **M10** | Brush selection (BVH, spine raycast, W clip) | geometry | §7 |
| **M11** | Re-fillet tagging (ReserveIDs + MeshGL stamp) — *riskiest* | geometry | §8 |
| **M12** | `fillet()` SCAD wrapper + preview passthrough | packaging | §2.1, §10 |

**Deferred** (unnumbered): analytic fast paths (§11), `runout=`, variable radius
along a chain, unequal-radius vertex blends (§9).

---

## Milestone detail

### M0 — Scaffold: node exists, callable, no-op

- New `src/core/FilletNode.{h,cc}` (or equivalent): node class + type enum for
  the four variants + `VISITABLE()`.
- Four `builtin_*` factories parsing `r`/`t` + curve params into a
  `CurveDiscretizer`; store children (child 0 = target, 1+ = brushes).
- `register_builtin_fillet()` wired into `Builtins.cc`.
- `visit(State&, const FilletNode&)` in `GeometryEvaluator.{h,cc}`: pull child 0
  via `collectChildren3D`, return **empty** geometry (a tool that does nothing
  yet — semantically honest, and matches the eventual preview-passthrough state).
- **Acceptance:** `fillet_tool(r=2) cube();` parses, evaluates, renders nothing,
  no crash; `.csg` dump shows the node.

### M1 — Visual test harness (hand-written references)

The point: an **inspectable image before the operator exists**, built from
hand-written OpenSCAD idioms that represent the *known-good* answer. Every later
geometry milestone swaps its real output into the same viewer, so we diff against
the reference by eye first, then automatically at M6. Also the artifact the user
uses to redirect direction early.

**Viewer convention** (one shared module). For each case, one image lays out
three things along X:

1. **Base model** at origin.
2. **Applied result** — `union(model, tool)` / `difference(model, tool)` — the
   finished filleted feature.
3. **Isolated tool solid** translated `+100` in X.

**Cross-cut style: thin slab.** Slice with a thin-slab `intersection()`
(kept as 3D but viewed down the section normal), plane **perpendicular to the
edge**. Clean flat section, best for measuring arc tangency / radius / setback.
The inner-90° case becomes a single picture: L-shaped corner section, finished
result with arc filled, and the standalone wedge-minus-arc tool — all readable at
a glance.

**Radius sweep.** Every case renders at two radii in a second row:
- `r_small` — comfortably within the surface.
- `r_large` — deliberately **≥ the surface extent**. This is where the
  interesting failures live: tool eating the next feature, clamp/warn (§6.5),
  `d = r/cos(φ/2)` blowup. Small vs. large sit side by side in one image.

**Junction cases via low-`$fn` cones.** `cylinder($fn=3, r1=R, r2=0)` gives a
3-face apex; `$fn=4` gives a 4-face apex. Two forms per junction:
- **convex apex** — the cone itself → `round_tool` target (faces meeting at a
  point in air).
- **concave pocket** — `difference(){ block; cone; }` → inside 3/4-face corner,
  the §6.3 / §6.3.1 target.

**Contour stack for junctions.** Instead of one slice, take N parallel thin-slab
sections at increasing distance from the vertex and translate each apart along a
layout axis — one image shows the section *evolving* toward the meeting point
(square-with-quarter-round far out, collapsing toward the corner sphere near
`P`). This makes a wrong corner (inscribed lump §6.3.3, gouge §6.3.1, spurious
block §6.3) obvious at a glance rather than only numerically.

**Reference tools are hand-written** (no operator dependency) and *are* the
M5/M7 acceptance targets:
- hole mouth → `annulus_prism − torus` (the §1 idiom)
- 90° inner corner → `wedge − cylinder(r)`; chamfer → `wedge` alone

**Case matrix:**

| case | slice style | radii |
|---|---|---|
| inner 90° corner (2 cubes), fillet + chamfer | single thin slab ⊥ edge | small + large |
| cube − cylinder through hole | single thin slab ⊥ edge | small + large |
| cube outer edge (`round_tool`) | single thin slab | small + large |
| 3-face apex — cone (convex) + pocket (concave) | contour stack | small + large |
| 4-face apex — `$fn=4` cone + pocket | contour stack | small + large |
| brush half-chain | single thin slab | small |

Rendered headless via the `openscad` CLI; PNGs are inspectable by both the user
and Claude.

- **Acceptance:** each case renders a legible reference image; radius sweep and
  contour stacks read correctly by eye.

### M2 — Mesh extraction + edge adjacency + classification

- Get child 0 as `ManifoldGeometry`, `GetMeshGL64()`.
- Build edge→2-face adjacency (rebuild halfedges from `triVerts`, vertex-merge by
  position).
- Classify each edge: far-vertex concave/convex test (§4.2) + `$fa`-derived angle
  filter via `CurveDiscretizer` (§4.3). Optional §4.4 provenance upgrade (same-
  `originalID` → skip) layered on top where present.
- No geometry out; `LOG` counts.
- **Acceptance:** cube → 12 edges / 6 faces, all convex; inside corner → concave.
  Counts match by hand.

### M3 — Debug visualization of classified edges

- Emit colored debug geometry (thin markers along accepted edges), colored
  concave / convex / rejected via `setColor`.
- **Acceptance:** on cube − cylinder, hole mouth lit, bore seams rejected;
  visually confirmed against M1 viewer.

### M4 — Chain walking + spine

- Walk accepted edges into chains via shared vertices (open / closed / branch),
  canonical geometric ordering (§5.1).
- Per-vertex averaged normals (§5.2); `C`, `TA`, `TB`, φ→180 guard (§5.3).
- Debug: draw spine polyline + `TA`/`TB` markers.
- **Acceptance:** closed hole-mouth = one ring; ordering stable under a small
  parameter nudge; matches the §5.3 worked floor/wall example numerically.

### M5 — `chamfer_tool` / `bevel_tool` (W only) — first real geometry

- Pentagon section (§6.2), hull consecutive sections, union via `BatchBoolean`.
  Two-face edges only, no junctions. Convex sign flip → `bevel_tool` (§6.4).
- Take the setback directly, not derived from `r` (§6.4).
- **Acceptance:** drops into the M1 harness; inner-corner chamfer matches the
  hand-written `wedge` reference at both radii.

### M6 — Automated regression harness

- Establish the ctest baseline-PNG pattern **once**: convert the M1 files (§1
  sanity case + chamfer cases) into committed `.scad` under
  `tests/data/scad/3D/features/` with committed baseline PNGs.
- **Acceptance:** new tests run under CTest; baselines committed; later geometry
  milestones only add a `.scad`.

### M7 — `fillet_tool` / `round_tool`

- Add U as **disc** hulls first (§6.1 fastest route), `W − U`. Two-face edges
  only, junctions untouched.
- Then swap discs → **circular segments** (§6.1 optimization) as a second commit;
  output unchanged within tolerance.
- **Acceptance:** hole mouth == `annulus_prism − torus` sanity (§1) in the M1
  harness and as regression.

### M8 — Junction cells, degree 3

- Corner ball `P` (3×3 solve), dual spine **and** wedge truncation, corner cell
  (§6.3).
- **Acceptance:** inside box corner closes; §6.3 symmetric check at `z=0.1r`;
  contour stack matches reference.

### M9 — Junction degree ≥4 + guards

- Q-vertex triplet hull with feasibility filter (§6.3.1); numerical guards
  (§6.3.2); runout fallback for rejected junctions.
- **Acceptance:** asymmetric valence-4, infeasible-triplet, and singular-solve
  cases behave (no gouge, no NaN, graceful fallback).

### M10 — Brush selection

- BVH over brush triangles, spine raycast, interval clip of **W only** (U
  overhangs one segment) (§7).
- **Acceptance:** half-chain brush → flat perpendicular cap, no scoop;
  empty-selection warning with counts.

### M11 — Re-fillet tagging (riskiest)

- Reserved ID range via `ReserveIDs` (§8); stamp the tool through the **MeshGL
  round-trip** (`runOriginalID`). Range membership test; optional hashed tags.
- **Acceptance:** fillet-then-fillet rejects with warning; `ignore_tags`
  re-enables; cache hits stay protected.

### M12 — `fillet()` wrapper + preview passthrough

- Bundled `.scad`: `fillet()` sugar over the four tool nodes (§2.1); preview
  passthrough `if (!$preview)` so preview shows base only (§10).
- **Acceptance:** preview shows base geometry only; F6 shows fillets; override
  flag works.

---

## Open design decisions carried into implementation

- M0 no-op returns empty (chosen) vs. passthrough — empty is semantically honest
  and matches preview passthrough.
- Debug viz (M3/M4): whether the colored-marker output stays as a permanent
  `$fillet_debug`-style flag on the real nodes (keeper, helps corner debugging)
  or is a temporary node deleted at M5. Leaning keeper.
- Everything under plan §13 "Open questions" (Manifold signatures, MeshGL
  round-trip exactness, branch-vertex reality, closed-chain seams,
  `linear_extrude`/`rotate_extrude` seam angles).
