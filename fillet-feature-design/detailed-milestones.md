# Fillet Operator — Detailed Milestone Split

Companion to `fillet-operator-plan.md`. That document is the design; this one is
the delivery plan: small, individually mergeable milestones, plus the
visual-inspection harness used to steer and verify the work.

Each milestone below is intended to be **one compilable, mergeable PR**.

> **Convention — no design-doc references in the source.** These design files
> (`fillet-feature-design/`, `fillet-operator-plan.md`, milestone/section
> numbers like "M2" or "§4.3") are scaffolding and get thrown away once the
> feature merges. Do **not** cite them from `.cc`/`.h` comments — code comments
> must stand on their own so they still make sense after the docs are gone.
> Explain the *reasoning* inline instead of pointing at a section number.

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
- **Copyright header on new files: use the dateless "OpenSCAD Developers" form.**
  Copy it verbatim from **`src/gui/ColorLabel.h:1-18`** (the repo's most recent
  new-file header) — `Copyright The OpenSCAD Developers.`, no year range, no
  CGAL linking exception, FSF URL rather than the stale Boston address. Do
  **not** copy the legacy `Copyright (C) 2009-20xx Clifford Wolf …` block that
  most older files carry. Two hard rules: **never add a contributor's own name**
  (the few individually-named files are pre-2022 subsystem drops), and **never
  bump the year** on an existing legacy block — those ranges are copy-paste
  artifacts nobody maintains. Coverage is uneven repo-wide (~40% of `.cc`, ~14%
  of `.h`; 0/27 headers in `src/geometry/`), and **no `_test.cc` has one** — so
  a missing header elsewhere is not a signal to omit yours.

---

## Pre-existing test failures (not ours — do not chase)

These fail on this machine on a **clean** `kerem-fillet` checkout, with our
changes stashed; they are unrelated to the fillet work (SVG arc-path export
text-diff, an environmental/precision issue). A full `ctest` run is otherwise
green apart from these three — treat them as the expected baseline:

- `export-svg_spec-paths-arcs01`
- `export-svg-fill-stroke_spec-paths-arcs01`
- `export-svg-fill-only_spec-paths-arcs01`

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
| **M6** | Automated regression: turn `fillet-tests/` cases into ctest baselines | test | §15 |
| **M7** | `fillet_tool`/`round_tool` — disc hulls, then swap to circular segments | geometry | §6.1 |
| **M8** | Junction cells degree 3 (`P`, dual truncation, corner cell) | geometry | §6.3 |
| **M9** | Junction degree ≥4 (Q-vertex) + numerical guards + runout fallback | geometry | §6.3.1-6.3.2 |
| — | Size validity: warn and drop a chain the size does not fit | geometry | §6.5 |
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

**Status: done, then restructured (2026-07-27).** The harness and the M6-prep
equivalence checker were merged into a single `fillet-tests/` directory. The
conventions above all survive — thin-slab slices, the radius sweep, contour
stacks for junctions, hand-written references as the M5/M7 acceptance target —
but a case is now **one file** declaring model, reference tool and operator call,
consumed by both the renderer and the headless checker, instead of being written
once as a scene and again as a comparison. Consequences worth knowing here:

- The viewer's three columns became six: the operator's applied result and its
  isolated tool now sit beside the reference's, and a final column shows the
  residual between them. The scenes call the operator; they are no longer
  operator-independent.
- Every case is automatically checked, not just eyeballed — including the
  junction cases, via a reference-free containment check, since the equal-radius
  corner has no closed form to compare against.
- Cases are one tool per file, so `chamfer_tool` and `bevel_tool` have their own
  rather than sharing the inner-corner scene. All four builtins are covered.
- The radius sweep is a per-case variant list; the large radius keeps its row and
  runs the reference-free checks, since §6.5 clamping is not settled.

See `fillet-tests/README.md` for the contract and
[`test-design.md`](test-design.md) §4 for the reasoning.

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

**Status: done.** `src/geometry/fillet/FilletBuilder.{h,cc}`, wired into
`GeometryEvaluator::visit(FilletNode)` (converts child 0 via
`ManifoldUtils::createManifoldFromGeometry`, guarded by `ENABLE_MANIFOLD`), plus
a `CurveDiscretizer::getMaxSeamAngle()` accessor for the §4.3 threshold. Emits
one `ECHO` line of counts per invocation on both the render and preview paths
(cached, so it re-fires only on a cache miss). Verified:

- cube → 18 edges (12 feature, all convex; 6 face-diagonals rejected at 0°).
- inner-corner union → 1 concave feature edge (`fillet_tool` selects it), plus
  convex outer edges (`round_tool` selects those).
- `cube − cylinder($fn=32)` hole mouth → 64 convex rim edges; the mouth is
  **convex** (rounded by subtraction), so `fillet_tool` correctly selects 0 and
  `round_tool` would take the rim.

**Two deviations from the plan, both deliberate:**

1. *"6 faces" was loose wording.* MeshGL's `originalID` is **per source
   primitive**, not per geometric face — a lone cube reports **1** surface, not
   6. The meaningful count (12 feature edges, all convex) matches. Provenance
   therefore keys on primitive, not face.
2. *Provenance (§4.4) is reported, not used to override.* A hard-`skip` on
   same-`originalID` edges wrongly drops a primitive's own sharp edges once it is
   unioned with anything (e.g. an L made of two cubes loses its outer edges,
   since each edge's two faces share that cube's id). The angle filter (§4.3) is
   the sole accept/reject decision; the same-surface count is logged as a
   diagnostic for later milestones. Same-id-but-sharp vs. same-id-coarse-curved-
   seam are indistinguishable by id alone, so provenance can't be a hard gate
   without curvature/continuity analysis — revisit in M3 if bore-seam rejection
   at low `$fn` needs it.

### M3 — Debug visualization of classified edges

- Emit colored debug geometry (thin markers along accepted edges), colored
  concave / convex / rejected via `setColor`.
- **Acceptance:** on cube − cylinder, hole mouth lit, bore seams rejected;
  visually confirmed against M1 viewer.

**Status: done.** `buildFilletTool` now returns a colored debug `PolySet`
instead of empty geometry: `fillet::detail::debugEdgeMarkers` straddles every
real edge with a thin box marker, colored **red** (concave feature), **green**
(convex feature), or **grey** (rejected tessellation seam), via per-polygon
`PolySet` colors. Coplanar triangulation diagonals (dihedral `< 1°`) are omitted
so flat faces stay clean; marker thickness is `1.2 %` of the mesh bounding-box
diagonal so it reads at any scale. Verified by eye on three cases:

- cube → 12 green edges, no diagonals drawn.
- `cube − cylinder($fn=32)` → both hole-mouth rims green (convex), vertical bore
  seams grey (rejected), outer cube edges green — the acceptance case.
- inner-corner union → the single reflex edge red, all others green.

The markers were the operator's whole output at this milestone. They are now
gated behind a `debug=` argument (default off), so the operator is an honest
no-op until it builds a real tool solid; a later milestone returns that solid
here and leaves the markers as the diagnostic view.

### M4 — Chain walking + spine

- Walk accepted edges into chains via shared vertices (open / closed / branch),
  canonical geometric ordering (§5.1).
- Per-vertex averaged normals (§5.2); `C`, `TA`, `TB`, φ→180 guard (§5.3).
- Debug: draw spine polyline + `TA`/`TB` markers.
- **Acceptance:** closed hole-mouth = one ring; ordering stable under a small
  parameter nudge; matches the §5.3 worked floor/wall example numerically.

**Status: done.** `fillet::detail` gains `selectedEdges` (the tool's edges:
threshold-clearing, concavity-matching), `buildChains` (walk into open/closed
chains via shared vertices, degree-2 interior vs degree-≠2 endpoints, canonical
ordering by vertex position so indices survive a parameter nudge), and
`spineFrames` (per-vertex averaged wall normals with fixed handedness along the
chain, then `C`/`TA`/`TB` and the φ→180 guard). `buildFilletTool` now overlays a
spine debug solid on the M3 edge markers: a blue cube at each ball center `C`,
orange/purple cubes at `TA`/`TB`, and yellow legs from the vertex to each
tangency point. Verified:

- floor/wall union → red concave crease; at each station `TA` on the floor, `TB`
  up the wall, `C` on the diagonal in the air. Unit test pins `C−v=(r,0,r)` and
  the tangency pair `{(r,0,0),(0,0,r)}` at `r=3`, φ=90° — the §5.3 numbers.
- `cylinder` (round_tool) → the two convex rims walk into two closed rings of
  `$fn` stations each (unit test); a continuous ring of frames renders.
- L-shape → the single concave crease is one open 2-vertex chain (unit test).

Handedness note: side A/B at a vertex is fixed by
`sign(cross(nA,nB)·walkdir)`, so averaging adds same-wall normals together along
a curved chain. The debug-marker gating question is settled: `debug=` argument,
default off.

### M5 — `chamfer_tool` / `bevel_tool` (W only) — first real geometry

- Pentagon section (§6.2), hull consecutive sections, union via `BatchBoolean`.
  Two-face edges only, no junctions. Convex sign flip → `bevel_tool` (§6.4).
- Take the setback directly, not derived from `r` (§6.4).
- **Acceptance:** drops into the M1 harness; inner-corner chamfer matches the
  hand-written `wedge` reference at both radii.

**Status: done.** `fillet::detail` gains `chainNormals` (the per-station averaged
wall normals, factored out of `spineFrames` since both constructions start
there), `wedgeSections` (the §6.2 pentagon at each station, setback taken
directly along the Gram-Schmidt in-wall directions), and `buildWedgeSolid` (hull
each consecutive pair of sections, `BatchBoolean` the cells). `buildFilletTool`
returns that solid for `chamfer_tool`/`bevel_tool`; the rounded tools still
return nothing, and `debug=` still wins over both. Convex is the same code with
both signs flipped — setback into the solid, `eps` overshoot into the air.
Verified:

- inner-corner chamfer and outer-edge bevel both go green on all three checks at
  both sizes in `fillet-tests/`; their expected-FAIL lines are gone from
  `expectations.txt` and the residual column of both pictures is empty.
- unit tests pin the §6.4 setback (`t` along each wall, not the inscribed radius
  `r·tan(φ/2)`), the wedge volume and bounding box on the floor/wall union, the
  cube's twelve bevels (removes material, leaves the bounding box alone), and a
  cylinder rim as the closed-chain case (two rings, genus 1 each).
- the unclamped large variant behaves as §6.5 predicts rather than failing: a
  40-cube beveled at `t=30` collapses to the diamond that is the intersection of
  the twelve half-spaces. Correct, and not what anyone wants — §6.5 clamping is
  still open.

**One thing the plan does not mention.** Consecutive cells meet along a shared
section face, so the union's inputs intersect in a set of zero measure. Manifold
handles it, but leaves a degenerate four-triangle shell behind per contact — no
volume, invisible to any later boolean, and enough to make `Genus()` report `-9`
on a two-ring rim. `buildWedgeSolid` therefore decomposes the union and keeps
only the components that enclose material. `Simplify()` does not remove them.
Worth remembering at M7: `U` is built the same way and will do the same thing.
`BatchBoolean` was otherwise uneventful — no determinism problem surfaced across
repeated runs of the case suite.

### M6 — Automated regression harness

- Establish the ctest baseline-PNG pattern **once**: convert the `fillet-tests/`
  cases that are green by then (§1 sanity case + chamfer cases) into committed
  `.scad` under `tests/data/scad/3D/features/` with committed baseline PNGs.
- Fold the two-sided containment comparison into the Catch2 layer, calling
  Manifold directly rather than porting the shell driver — see
  [`test-design.md`](test-design.md) §4.5. That also drops the CGAL Minkowski
  cost the shell version pays.
- `fillet-tests/` stays afterwards as the place a case is prototyped before it
  has a baseline.
- **Acceptance:** new tests run under CTest; baselines committed; later geometry
  milestones only add a `.scad`.

**Status: done.** Two `.scad` cases under `tests/data/scad/3D/features/` —
`chamfer-tool-tests.scad` (inner corner, plus the boss-base ring as a closed
chain) and `bevel-tool-tests.scad` (all twelve cube edges, plus a cylinder's two
rims) — each showing the tool alone beside the applied result, so a change in the
tool itself is visible rather than hidden under the model. The glob picks them up
as 16 test instances with no CMake edit beyond the backend question below.
`src/geometry/fillet/FilletCompare_test.cc` carries the containment comparison,
and `expectations.txt` is unchanged: the wedge cases it covers were already green
there, and the rounded tools are still red for the same reason.

**The §1 sanity case is not among them.** It is a `round_tool` hole mouth, and
the rounded tools emit nothing until M7 — so M6 promotes the chamfer and bevel
cases only, and M7 adds the sanity case as one more `.scad`, which is the point
of establishing the pattern here.

**Two things the plan did not anticipate:**

1. *The backend question resolves differently than §3 of the test design
   expected.* The "fillet tools require the Manifold backend" warning is behind
   `#ifdef ENABLE_MANIFOLD`, a **compile-time** guard — `--backend=cgal` on a
   Manifold-enabled build still builds the tool through Manifold and renders it.
   Preview and throwntogether come out byte-identical on both backends, and so
   does the chamfer render, which therefore shares one baseline. What does differ
   is the **bevel** render: the tool's eps overshoot past each wall leaves
   slivers that Manifold accepts and CGAL's Nef conversion does not, so
   `render-cgal` discards 14 facets and shades the cut faces as back-facing. That
   case goes in `RENDER_DIFFERENT_EXPECTATIONS` — the same list, and the same
   reason, as `issue1137.scad` — rather than being hidden from one backend, so a
   change in either direction still shows up. The `disable_tests_safe` call the
   plan expected is instead in the `NOT ENABLE_MANIFOLD` block, where the
   compile-time fallback genuinely does make the images wrong.
2. *The containment comparison is much cheaper in C++ than in the shell.* The
   shell harness dilates with `minkowski()`, which has no Manifold path and falls
   back to CGAL's Nef kernel — seconds per check, and the reason ring cases run
   at `$fn = 48`. Written against Manifold directly, the dilation is one convex
   hull per triangle (a triangle plus a cube is the hull of its 24 offset
   corners, so this is exact, not an approximation) and the four cases together
   run in about two seconds. Included is the calibration the shell suite carries
   as `selftest_*`: two tessellations of one cylinder agree within the chord
   error and not within a tenth of it, a solid never agrees with an empty one,
   and a wedge 20 % too deep is caught.

Setting the layer up was also the moment to close the unit-test gaps left since
M2: the `$fa`/`$fn`-derived threshold (the number, and the seam rejection it buys
at 8/16/64 facets), the φ→180 guard on a half-degree slit, non-positive and empty
input, and the debug overlays by colour class. Everything in the internal header
now has a test; the node factory and `buildFilletTool` are covered by the `.csg`
dump and the regression images instead — see `test-design.md` §2.3.

### M7 — `fillet_tool` / `round_tool`

- Add U as **disc** hulls first (§6.1 fastest route), `W − U`. Two-face edges
  only, junctions untouched.
- Then swap discs → **circular segments** (§6.1 optimization) as a second commit;
  output unchanged within tolerance.
- **Acceptance:** hole mouth == `annulus_prism − torus` sanity (§1) in the M1
  harness and as regression.

**Status: done.** `fillet::detail` gains `roundSections` (the wedge pentagon and
the canal section at each station) and `buildRoundSolid` (hull consecutive pairs
of each, subtract, union the chains); `spineFrames` gains a `concave` flag.
`buildFilletTool` returns that solid for `fillet_tool`/`round_tool`, with the arc
tessellation taken from the discretizer at radius `r`. The setback is no longer a
parameter for these tools: it is `r·tan(φ/2)` per station, so it follows the
local wall angle instead of being one number for the chain. Verified:

- the §1 sanity case is green — as a `fillet-tests/` case, as one of the two new
  `.scad` regressions, and as a Catch2 comparison against `annulus_prism − torus`.
- all four small `ref` variants in `fillet-tests/` pass every check, and so do
  both radii of the boss base, whose fillet is twice the radius of its boss.
- unit tests pin the inner corner, one cube edge, the hole mouth and that boss
  base against hand-written references, plus a probe at a cube corner (below).

**Three deviations from the plan.**

1. *The tangency frame has a sign.* `spineFrames` was written for a concave
   crease, where both outward normals point into the empty quadrant; at a convex
   one the ball sits inside the solid and the centre offset and both tangency
   offsets flip. Nothing caught it at M4 because the only reader was the debug
   overlay.
2. *The section plane is spanned by the two wall normals, not perpendicular to
   the spine.* §6.1 treats these as the same thing. They are, on a straight edge;
   around a bend only the first still contains `C`, `TA` and `TB` by
   construction. The boss base is the case that fails on the other choice.
3. *`W − U` is per chain.* Subtracting every chain's canal from every chain's
   wedge lets the ball rolling along one crease hollow out the bead of a crease
   it meets — a cube came out with a lump on all eight corners. Within a chain
   the subtraction still has to span the whole wedge (a cell's ball reaches into
   the next cell's wedge at a bend); between chains it must not. What belongs at
   a meeting point is the corner cell of M8.

**The circular-segment optimization was written and backed out.** It is correct
and it is smaller (2366 → 2087 vertices on the hole mouth), but the segment's
chord is the wedge's own outer face, so `W − U` puts two exactly coincident
surfaces against each other along the whole tool and both closed-ring cases stop
being dilatable by CGAL. Pushing the chord past by an `eps`, the way the pentagon
does at the walls, rescues the hole mouth at twenty times the wall `eps` and
never rescues the boss base — and the safe depth is not a constant anyway, since
the hull between two stations overhangs each station's chord plane by an amount
that depends on how the spine turns. The full disc covers that for free: the
major segment it carries is the rest of the ball. Worth reopening at M8, where
junction spheres meet the same faces. See
[`log-2026-07-28-m7.md`](log-2026-07-28-m7.md).

### M8 — Junction cells, degree 3

- Corner ball `P` (3×3 solve), dual spine **and** wedge truncation, corner cell
  (§6.3).
- **Acceptance:** inside box corner closes; §6.3 symmetric check at `z=0.1r`;
  contour stack matches reference.

**Take the per-chain `W − U` back out here.** M7 subtracts each chain's canal
from its own wedge only, because an untruncated canal runs its ball centres all
the way to the corner *vertex* and so reaches sideways into the wedge of every
chain it meets. Spine truncation is the actual fix: once the last station moves
back to `P`, the canal stops there and `U` becomes exactly the set of legal ball
positions, at which point a single global subtraction is not merely safe but
required — the corner ball has to cut all three chains' wedges, and per-chain
subtraction would stop it, leaving the inscribed lump of §6.3.3.

So this is a decision to revisit, not a workaround to preserve. The probe test in
`FilletCompare_test.cc` stays valid across the change: its box sits 3.35–4.0 from
`P` at `r = 3`, hence outside the corner ball, so it survives a global
subtraction the moment truncation lands. If it goes red when the grouping is
removed, truncation is what is wrong, not the grouping.

**Status: done.** `fillet::detail` gains `Junction` and `chainJunctions`, and
`RoundSection` gains the ball centre `C` so a section can be interpolated to a
truncation point. `buildRoundSolid` now truncates every spine, adds a corner cell
and a corner ball at each junction, and subtracts once globally. The per-chain
grouping is gone and the probe test is green without it. `buildWedgeSolid` is
untouched — overlapping wedges are what a chamfered corner is, so those tools
need no junction cell. Verified against exact references (below): the cube's
twelve edges and eight corners, a three- and a four-face apex, a three- and a
four-face pocket, the inside box corner, and the §6.3 section at `z = 0.1r`.

**Three things worth carrying forward.**

1. *The truncation rule replaces the solve, not the other way round.* Stating it
   as "stop where the ball first reaches any wall of the junction" — the §6.3
   universal form — needs no 3×3 solve, no valence case, and no fallback when a
   solve is rejected. `P` then only exists to place the corner ball. The plan's
   own framing, but it is worth saying that following it literally (solve first,
   truncate at the result) is the harder implementation.
2. *Degree ≥ 4 came forward from M9, because truncating without a corner there
   was a regression.* The general construction — solve every triple, keep the
   centres clear of the remaining walls, hull balls at what is left — is smaller
   than a degree-3 special case plus an exception, and degree 3 falls out of it.
   M9 keeps asymmetric valence-4 (untested here), unequal radii, and the runout
   fallback.
3. *`refRoundedConvex` is the reference to reuse.* Rounding a convex solid is
   exactly eroding it by `r` and dilating by a ball of `r`, and both are
   computable from the solid's own mesh — face planes pushed in, then the hull of
   balls at the eroded vertices. One comparison pins every edge and every corner
   at once, and the concave cases apply it to the void.

**What is red, and why.** A bare `round_tool()` solid cannot be converted to a
Nef polyhedron: at every corner a sphere sits tangent to three walls, and the
surfaces meeting along those tangencies leave triangles too small to survive
CGAL quantising its input. *Applying* the tool is fine on both backends;
`round-tool-tests.scad` shows the bare tool on purpose, so
`render-cgal_round-tool-tests` and `render-csg-cgal_round-tool-tests` are
disabled with the reason recorded. Reducing the tangential contact — the
circular-segment section M7 backed out is the obvious candidate — is the real
fix. The two apex cases in `fillet-tests/` also fail their `sandwich` check, and
correctly: rounding a sharp apex moves the surface by much more than `r`, so a
reference-free "stays within `r`" check cannot pass there whatever the operator
does. See [`log-2026-07-28-m8.md`](log-2026-07-28-m8.md).

### M9 — Junction degree ≥4 + guards

- Q-vertex triplet hull with feasibility filter (§6.3.1); numerical guards
  (§6.3.2); runout fallback for rejected junctions.
- **Acceptance:** asymmetric valence-4, infeasible-triplet, and singular-solve
  cases behave (no gouge, no NaN, graceful fallback).

**Mostly landed at M8**, because degree 3 could not be special-cased without
regressing a degree-4 case. In: the triplet enumeration, the feasibility filter,
the hull of the kept balls, and the §6.3.2 guards (singular determinant,
`|P − v| > 10r`, finiteness).

**Asymmetric valence 4–6 is covered and green.** Shearing a low-`$fn` cone moves
its apex off the axis, so the offset planes stop sharing a point and the ball
gets several extreme positions — four sides give two, five three, six four — and
the result matches the exact rounded solid at every one, convex and concave. The
filter is doing real work rather than passing everything through: it is what
keeps a triple that is tangent to its own three walls but buried in a fourth from
gouging the fillet back from that fourth wall.

What was left after M8 was two holes and one non-junction question. **All three
are closed now**, along with the size gate they kept running into:

1. **Mixed-sign vertices — the solve hears every wall now.** The constraint set
   was built from the walls of the *selected* creases only, so a convex edge
   arriving at a concave corner was never heard of even though its far face
   bounds where the ball may sit. `chainJunctions` now collects the normals of
   **every triangle incident to the vertex** instead. It is a strictly larger
   constraint set — the walls the creases ride are a subset of it — so nothing
   that was feasible before and genuinely clear of the neighbourhood has moved,
   which is what the exact corner comparisons confirm by staying green. The four
   Group A cases were already clean before the change; what changes is that they
   are clean *by construction* rather than by the extra face happening not to
   bind.
2. **A junction that gets no centre now runs out instead of stopping short.**
   Truncation used to run off the wall normals without asking whether a corner
   cell had been built, so a rejected solve left every incident spine cut back
   with nothing filling the space — the needle's beads stopped eighty radii below
   its apex, silently. Those ends are no longer truncated at all: the radius
   ramps to zero over the last `2r` of each incident spine, the last section is
   the sharp vertex itself, and the beads converge on it. The result is a valid
   solid whose blend fades out locally, which is §6.3.1's runout, and a warning
   says the corner is not the constant-radius one that was asked for.
3. **Unequal radii need no detect-and-warn yet.** A tool node carries one size
   for the whole invocation, so unequal radii at a vertex cannot be *expressed* —
   there is nothing to detect. The warning belongs with the feature that would
   create the situation (per-edge sizes, or variable radius along a chain), not
   before it.

**Where the runout and the size gate meet.** The needle is the case the runout
was written for, and running it through the finished operator shows the two in
the right order: the size gate refuses the three slant creases first, because the
spike tapers below the tool's own reach for its last 27 mm, and only the three
base creases are built. The runout is what would happen at that apex if the size
did fit — which the unit test exercises directly, since it calls the builder
without the gate in front. The two mechanisms answer different questions: "can
this size be built here at all" comes before "what happens at a corner nobody can
seat a ball in".

**Not a junction problem, but it is what the high-valence cases failed on
first.** The sheared six-sided pyramid has a crease at 130°, where the tangency
setback is over twice `r`; past `r ≈ 2` on a 30 mm feature the neighbouring beads
collide and the answer stops being the naive rounded solid. That is the size
gate, below, and it is now refused rather than built.

### Size validity — warn and drop (plan §6.5)

Not one of the numbered milestones; it is the decision §6.5 has carried since the
plan was written, and it had to land before M10 because every case that asks for
too much reaches it first. The decision itself was settled earlier — **warn and
drop the chain, never clamp, never vary the size automatically**; see
[`log-2026-07-28-oversize-radius.md`](log-2026-07-28-oversize-radius.md) for why
a clamp cannot stay local. What landed now is the check behind it.

Two questions per chain, both answered from contact points rather than from
volumes, so nothing has to be built to find out that it cannot be:

- **Does the tool still touch the model?** The point where the blend meets each
  wall has to land on that wall. A surface here is the whole smooth patch — a
  bore's facets are one wall, a cube's faces are six — found by joining triangles
  across every edge whose dihedral falls below the same threshold that separates
  creases from tessellation. When the contact point is off the end of its wall,
  no blend of the size asked for exists there: the arms of an L 30 mm long cannot
  carry `r = 35` at any tessellation.
- **Is the room it needs its own?** Another crease's contact line sitting inside
  this crease's seated ball is that crease's bead being eaten. That is the
  "nearest other feature" half of §6.5, and it is what refuses `r = 30` on a
  40 mm cube (the edge across the face reaches back the other way) and `r = 8`
  between two walls 10 mm apart. Creases that meet at a junction are exempt:
  sharing material there is exactly what a corner cell is.

**Four things worth carrying forward.**

1. *A crease has to be sampled along its length, not only at its stations.* The
   spine's stations are mesh vertices, and on a tapering feature the room
   available between two of them falls below what the tool needs without either
   station noticing — a spike whose only two stations are its base and its apex
   is the extreme case. The gate samples three points per segment as well.
2. *The two ends of an open chain are exempt from the touching test.* A crease
   stops at the boundary of its own walls, so the contact point there sits in the
   corner of the wall and steps out of it for reasons that are not about the
   size. On a tetrahedron it is unmissable: the base triangle's corners are 60°,
   so stepping perpendicular to one base edge leaves through the next, and every
   cone in the suite was refused until the rule went in. A size that genuinely
   does not fit fails *along* the crease, which is what the samples see. The
   crowding test keeps the ends, because there the ball's own reach is the
   subject rather than its footing.
3. *A sample is measured against its segment's walls, not its station's.* Taking
   the wall from the station survives until a chain turns a corner — the bead
   around a rib's foot is a closed loop, and half of every segment was being
   measured against the wall the previous segment rode, reporting an overshoot
   that grew linearly along it.
4. *The tolerance scales with the distance to the ball centre, not the radius.*
   A contact built from averaged normals lands at a mitre between two walls
   rather than on either, and the miss is an angle applied at the centre — so its
   size is set by how far that centre sits from the crease, which on a nearly
   flat crease is many radii (`r / cos(φ/2)` is 28 r at 176°). In terms of the
   radius alone the slack was an order of magnitude short, and a 60 × 2 cone was
   refused a round of 0.6 that fits it comfortably. The same quantity absorbs the
   tessellation's own error and the "over the limit by less than float noise,
   clamp silently" exception §6.5 asks for.

The wedge tools go through the same gate: a setback `t` at a crease of angle φ is
the ball of radius `t / tan(φ/2)` seated there, which is the same object to ask
how much room it needs.

**Acceptance:** the five `drop` variants in `fillet-tests/` — the oversized L
fillet and chamfer, the cube's round and bevel, the hole mouth — all go green on
`drops`, which wants an empty tool *and* the warning. The `* * drops FAIL`
catch-all is gone from `expectations.txt`.

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
- Debug viz (M3/M4): **decided — keeper, gated behind a per-node `debug=`
  argument**, not a `$fillet_debug` special variable. Rationale from a source
  dive: the repo has **no** debug-only `$`-var precedent (`$preview` is the only
  boolean special var, and it is render state, not a debug toggle), and a special
  var needs plumbing through `RenderVariables.{h,cc}` plus both construction
  sites (`openscad.cc`, `MainWindow.cc`). A per-node argument matches how every
  operator takes options, reads in the factory with
  `node->debug = parameters["debug"].toBool();`, needs zero global plumbing, and
  toggles per call (isolating one bad corner). At M5 `buildFilletTool` returns
  the markers when `node.debug` is set and the real tool solid otherwise. The
  other mechanisms surveyed (`--debug=<cat>` log channel, `#ifdef DEBUG`, env
  vars) either can't emit geometry or aren't user-toggleable at runtime.
- Everything under plan §13 "Open questions" (Manifold signatures, MeshGL
  round-trip exactness, branch-vertex reality, closed-chain seams,
  `linear_extrude`/`rotate_extrude` seam angles).
