# Mixed-corner vertex blend — implementation plan

A concrete plan for replacing the mixed-sign corner **loft** (`emitCoonsSaddle`,
fallback `emitSaddle` in `src/geometry/fillet/FilletBlend.cc`) with a genuine
**multi-surface rolling-ball vertex blend**: the convex round-over surfaces and the
concave fillet surface meet along their true surface–surface intersection (trim)
curves, mesh-native, one pass, no boolean kernel.

Companion causal theory: `lbracket-saddle-theory.md`, `mixed-corner-saddle.md`,
`corner-defect-taxonomy.md` (defect ②). This document is the *forward* plan.

> **RESULT (Phase 0 + 1 built and reverted, 2026-08-15): the sail is intrinsic —
> keep the loft.** Phase 1 was implemented for the canonical lbracket/box_step
> elbow and produced a *worse* shape (spike + flat facet), then reverted to HEAD.
> The obstruction, grounded in the numbers, is in §7 below: at these radii the
> concave fillet cylinder and the convex corner **share no point**, so the corner
> sits in a gap **no rolling-ball surface reaches**. That gap must be filled by a
> transfinite patch — and that patch *is* the sail. Trim-curves-first can route
> the internal edges but cannot remove the gap; the existing loft
> (`emitCoonsSaddle`) is already a reasonable tangent-continuous filler for it.
> Do not rebuild Phases 1–4 as specified below. The only path with any upside is
> improving the loft's filler in place (§7) — Phase-3-scale, marginal payoff.

---

## 0. The spine: sort the edges first, fill second

The loft and the base-bowl both **fill first and let the internal edges fall where
the fill puts them**. The loft (`emitCoonsSaddle`, L1232) partitions the ring into
one concave arc `cc` and the complementary convex run `cv` (L1251–1263), then lofts
`cc`→`cv` by an arc-length merge (L1355–1370). Because a mixed ring's convex run is
~2× its concave arc (the convex round-overs span two edges of the front-face corner,
the concave crease one) the merge **fans the short arc across the long run** — and
that fan *is* the sail. Parameters cannot remove it; it is a property of lofting two
boundaries of unequal length with the internal edges unconstrained.

The fix inverts the order:

1. **Enumerate** the rolling-ball surfaces incident at the vertex (one per selected
   edge: a convex round-over cylinder, or a concave fillet cylinder).
2. **Trim** — compute the *pairwise* surface–surface intersection curves and lay the
   internal tessellation edges **on** them. This is where the edges *should* go: each
   convex round-over runs up to its true meeting curve with the concave fillet and
   they join along an actual edge, not a fanned diagonal.
3. **Assemble** the internal edge skeleton (the trim curves) plus the outer ring
   (the strips' end cross-sections, welded at 1e-6).
4. **Fill** each bounded sub-region — each is bounded by ring arcs + trim curves —
   with a simple constrained patch. Because every fill is bounded by *real surface
   boundaries* instead of fanning across them, no sail can form.

The hardness does not disappear; it **moves into step 2** (robust general trim-curve
computation). §3 assesses which vertex classes make step 2 tractable and which must
fall back to the loft.

---

## 1. Geometry of the vertex blend (canonical planar 2-convex / 1-concave, 90°)

Canonical case: the **L-bracket elbow**, `fillet-bench/models/lbracket.scad`
(`cube([40,30,6]) ∪ cube([6,30,40])`, `r=2`), front-face vertex **u = (6, 0, 6)**.
`box_step.scad` (`r=3`) is the same topology, step-foot vertex **(24, 0, 10)**. Three
selected feature edges meet at u:

| Edge | Direction from u | Faces | Sign | Rolling surface |
|------|------------------|-------|------|-----------------|
| e_X  | +X → (40,0,6)    | front (n=−Y) ∧ step-top (n=+Z) | **convex** | Cyl_X, axis ∥X through (·, r, 6−r) |
| e_Z  | +Z → (6,0,40)    | front (n=−Y) ∧ side (n=+X)     | **convex** | Cyl_Z, axis ∥Z through (6−r, r, ·) |
| e_Y  | +Y → (6,30,6)    | step-top (n=+Z) ∧ side (n=+X)  | **concave** | Cyl_Y, axis ∥Y through (6+r, ·, 6+r) |

Each surface is the plain rolling-ball construction the code already builds: for a
cross-section the rolling-ball **centre** is `filletCenter(u,t0,t1,concave)` (L731),
one radius off the seat along the sector normal, and every ring/arc point lies one
radius from it — the ring point's surface normal is `(p − C)` (used at L1630 and read
back in the loft at L1286). The axis line of the cylinder is that centre swept
parallel to the edge direction `(m.pos[x] − m.pos[u])`.

**The two-sign obstruction (why no single sphere/bowl works).** The convex centres
sit on the *material* side (Cyl_X at y=+r, z=6−r; Cyl_Z at x=6−r, y=+r), the concave
centre sits in the *air* quadrant (Cyl_Y at x=6+r, z=6+r). A convex arc bulges
*inward* on its own convex sphere; the concave arc bulges *outward* into the valley.
No single pivoting ball is tangent to all three faces with a consistent inside/outside
sign (this is the base-bowl failure). The surfaces are genuinely distinct and must be
trimmed against one another.

**The trim curves.** Three pairwise intersections, each a space curve of points at
distance r from *both* axis lines:

- **T_XZ = Cyl_X ∩ Cyl_Z** — the convex–convex seam. Two equal perpendicular
  cylinders sharing the plane y=r; their intersection is the diagonal ridge over the
  front-face 270° corner (a Steinmetz seam). This is the ridge where the two
  round-overs meet.
- **T_XY = Cyl_X ∩ Cyl_Y** — step-top round-over meets concave fillet. On the
  step-top face (z=6) Cyl_X's footprint is the strip y∈[0,r]; Cyl_Y's footprint is the
  strip x∈[6,6+r]; they overlap in the corner square (x∈[6,6+r], y∈[0,r]) and T_XY is
  the curve through it.
- **T_ZY = Cyl_Z ∩ Cyl_Y** — side round-over meets concave fillet (mirror of T_XY on
  the side face x=6).

**Final patch layout.** The vertex region is the ring interior. Inside it:
- Cyl_X region: bounded by the e_X boundary arc (on the ring) and trim curves T_XZ,
  T_XY.
- Cyl_Z region: bounded by the e_Z boundary arc and T_XZ, T_ZY.
- Cyl_Y region: bounded by the e_Y (concave) boundary arc and T_XY, T_ZY.
- The three trim curves meet near one **central point** P* (the vertex-blend apex —
  for the convex side this is a point of the corner sphere; for a mixed corner it is
  the saddle apex where all three surfaces would coincide). A small residual gap
  around P* is closed last.

So the concave fillet no longer fans across the whole convex run; it fills only *its
own* lens between the concave arc and {T_XY, T_ZY}, and the two convex round-overs
fill their own lenses. The sail cannot appear because no fill spans two surfaces.

```
        e_Z arc (convex, on ring)
           \   Cyl_Z region
      T_XZ  \______ T_ZY
      (ridge)\  P* /
   Cyl_X      \  /   Cyl_Y region
   region  ____\/____  (concave)
      e_X arc /  T_XY  \  e_Y arc
             (convex)  (concave, on ring)
```

---

## 2. Mesh-native construction (one pass, no booleans)

Entry point: a new `bool emitVertexBlend(ring, ringNrm, ringSign, cornerPos)` called
from `emitCorner` (L1677) *before* the `emitCoonsSaddle || emitSaddle` line. It
returns `false` for any ring it does not accept, and the existing loft chain runs
unchanged — this is the mandatory fallback (§4).

### 2a. Enumerate the incident surfaces (reuse existing machinery)

`emitCorner` already walks the fan and builds `ring`, `ringNrm`, `ringSign` (+1
convex arc / −1 concave arc / 0 connector) and `cornerPos` (the sector-inset joins),
L1601–1637. Partition the ring by `ringSign` runs into **boundary arcs**, one per
incident selected edge — exactly the `cc`/`cv` split `emitCoonsSaddle` already does
(L1241–1263), but keep *each convex edge as its own arc* rather than merging both into
one `cv` run (the merge is what forces the fan). For each arc recover its surface:
- centre/axis: `C_i = out.V[ring[i]] − size * ringNrm[i]` (the reconstruction at
  L1286); the axis direction is the edge direction from the fan.
- radius: `size`.
- sign: `ringSign`.

Accept only the strict canonical class in Phase 1: **exactly 3 arcs, signs
{+,+,−}, near-90° well-conditioned axes** (see §3). Otherwise return `false`.

### 2b. Compute the pairwise trim curves discretely

For an ordered pair of cylinders (A with axis line ℓ_A, B with axis line ℓ_B, both
radius r), the trim curve is `{p : dist(p,ℓ_A)=r ∧ dist(p,ℓ_B)=r}` on the relevant
quarter-sheets. Mesh-native, robust procedure:

1. **Sweep A inward.** Reuse the cross-section builder: A's boundary arc is its
   cross-section at u; generate `K = arcSegs` further cross-section arcs stepping A's
   station *toward and just past* u along ℓ_A (each arc is `arcSegs+1` points, matching
   the strip — same count constraint as `crossSectionEdge`, L750). This is A's local
   surface as a `(K+1) × (arcSegs+1)` grid.
2. **Implicit-B march.** Define `g_B(p) = dist(p, ℓ_B) − r`. On A's grid, `g_B`
   changes sign across the seam; linearly interpolate the zero-crossing along each grid
   line to get a polyline — the trim curve **on A's tessellation**. Do the symmetric
   march on B's grid; average / snap the two so both regions index **one** shared
   polyline (a single `out.add` sequence — cracks otherwise, §4).
3. **Endpoints.** One end of each trim curve lands on the **ring** (where the two
   surfaces' footprints meet on their shared face — this is a connector/mitre point
   `cornerPos`, already a ring vertex, so it welds for free). The other end is the
   central apex P* where the third surface also arrives. Snap the three trim curves'
   inner ends to one welded P* (mean of the three inner samples, projected toward the
   corner-ball centre for the convex apex).

Sampling count is tied to `arcSegs` so the whole build stays `$fn`-driven and every
sub-region's shared boundary carries equal point counts (the strip-closure invariant,
L416–418).

### 2c. Assemble the internal edge skeleton + outer ring

- Outer ring: the strips' end cross-sections, **unchanged** (`out.add` at 1e-6,
  L106) — never resampled, so no T-junctions against the strips (the hard constraint).
- Internal edges: the three trim polylines T_XZ, T_XY, T_ZY, each welded once, ending
  on ring vertices and on the shared P*.

### 2d. Fill each bounded sub-region

Each region is a curved quad bounded by {one ring arc, two trim curves}. Because both
the ring arc and the trim curves lie **on the same cylinder**, the region is
developable — fill by a constrained loft *between the ring arc and the trim curve of
the same surface*, both of comparable length and both real boundaries of that surface:
no fan, no sail. Concretely, stitch the region's ring arc to its adjacent trim curve
with the same `stitch` arc-length merge the loft already uses (L1341), but now the two
sides belong to one surface so the merge is short-to-short. The central gap around P*
(a small triangle/`n`-gon of the three trim inner ends) closes with `earClipRing`
(L1025) — the pole-free close the loft theory asks for.

Reused verbatim: `out.add`/`out.tri`/`fan` (L106–133), `slerpUnit` and `emitCap`'s
spherical fill for the convex apex (L917), `filletCenter` (L731), `earClipRing`
(L1025), the crossSection arc builder (L750), the `edgeUse`>2 non-manifold self-check
(L1372–1378), and the `flush`/face-plane clamp against self-intersection (L788–796,
L1301).

---

## 3. Generality analysis (the hard requirement)

Step 2b is the crux. Classification of which rings take the vertex blend vs fall back:

| Vertex class | Example model | Step-2 trim computation | Verdict |
|---|---|---|---|
| **3 surfaces, 2 convex + 1 concave, planar, ~90°** | lbracket (6,0,6), box_step (24,0,10), rib end, refused_neighbour | perpendicular equal cylinders; trim analytic & well-conditioned | **Phase 1: vertex blend** |
| **Oblique dihedrals (non-90°)** | tee_oblique (80°), cross | cylinders still, axes non-perpendicular; trim solvable but **conditioning degrades as the dihedral shallows** (surfaces graze → trim near-tangent, ill-defined) | Phase 2 with a condition gate; else **fall back** |
| **Curved / tessellated walls** | mixed_fn ($fn 48∧10), boss_plate/hole_plate at low $fn | the "cylinder" is a **discrete per-facet rolling surface** (`sectorOf` is per-facet, L592); trim must intersect a *strip of facet-cylinders* and snap endpoints to facet seams | Phase 4, high risk; **fall back** initially |
| **T/cross stars (≥3 creases)** | tee, cross, rib_into_boss | k>3 incident surfaces → C(k,2) candidate trims; which trims actually bound a region is a **planar-arrangement** problem (occlusion, spurious intersections) | Phase 3, high risk; **fall back** |
| **≥2 concave edges at one vertex** | rib (two concave feet), pocket | concave∩concave trim + convex trims; more sign combinations, central region an n-gon | Phase 3; **fall back** |
| **Brush / one-sided partial selection** | brush_one_edge, refused_neighbour partials | a kept-sharp edge means the vertex is not fully blended; already excluded from `mixedVerts` (`computeMixedVerts`, L474–477) | **not a vertex blend** — kept-seam + loft, unchanged |

The classification is deliberately conservative: **Phase 1 fires only on the strict
canonical class**; `emitVertexBlend` returns `false` for everything else and the
existing chain (`emitCoonsSaddle` → `emitSaddle` → `ringSaddle` → `fan`) runs exactly
as at HEAD.

---

## 4. Risk budget

- **Manifoldness / cracks.** The one hazard unique to this construction: a trim curve
  shared by two regions must be the *same* welded polyline, or the two fills leave a
  gap. Mitigation: compute each trim once, both regions index the same `out.add` ids;
  end-snap to ring vertices and to a single P*. The outer ring is never resampled, so
  no T-junctions against the strips. Backstop: the existing `orient()` boundary-edge
  count (L138) and `nonManifoldEdges()` (L197) already reject any holed/finned result
  in `buildOn` (L1937) → the loft attempt runs instead.
- **Self-intersection.** A trim-region fill could bulge through an incident face at a
  crowded corner. Reuse the convex-arc face-plane clamp (L788–796) and the loft's
  `flush`-onto-surface clamp (L1301) on interior samples; boundary layers untouched.
- **Determinism.** Surface ordering and the aFirst tiebreak must be positional, not
  adjacency-order — mirror `filletCenter`'s lexicographic normal tiebreak (L742) for
  choosing trim-pair orientation and P* projection, so the mesh is byte-identical run
  to run.
- **$fn robustness.** Trim sample count = `arcSegs` (L416); every sub-region boundary
  carries equal point counts, so the patch stays watertight as `$fn` scales, exactly
  as the uniform-arc invariant guarantees for the strips.
- **Mandatory fallback (never worse than HEAD).** `emitVertexBlend` returning `false`,
  or `buildOn` detecting boundary/non-manifold edges, drops to the current loft. The
  multi-tier driver (pullIn tight → full mitre → baseline → raw mesh, L1976–1980) is
  untouched. The bench's all-VALID state is preserved: any ring the blend cannot handle
  cleanly renders via the loft.

---

## 5. Phasing + effort

| Phase | Delivers | Risk | Effort |
|---|---|---|---|
| **0. Gate + skeleton** | `emitVertexBlend` entry returning `false` (pure loft), the surface-enumeration/partition from the ring, unit tests on the partition | low | S |
| **1. Canonical planar 90°** | trim-curves-first + constrained fill for {+,+,−} perpendicular; removes the sail on lbracket/box_step/rib-end/refused_neighbour (S3-T01/02/03/12) | medium | **L** (the bulk: trim march, shared-weld, central close) |
| **2. Oblique dihedrals** | condition-gated trim for non-90° planar (tee_oblique, cross); else fall back | medium–high | M |
| **3. n-surface / ≥2 concave / T-cross stars** | arrangement of C(k,2) trims + n-gon central fill | **high** (arrangement robustness, manifold hazards) | XL |
| **4. Curved / tessellated walls** | discrete per-facet rolling-surface trims (mixed_fn, low-$fn bosses) | **high** | XL |

Each phase is bench-swept; mixed corners judged on the sheet (they are already VALID),
so the gate is the shading, not `mesh.py`.

---

## 6. Honest recommendation

**Build Phase 0 + Phase 1 only. Stop there.**

- **Is it worth doing at all?** The sail is a *subtle* shading artifact at normal
  viewing scale — every mixed-corner tile is already **VALID and manifold** (taxonomy
  ②, "quality artifact, not a topological failure"). This is polish, not a correctness
  fix. So the bar is: a *small, low-risk* slice with a clear visual win, and a hard
  fallback that cannot regress the all-VALID bench.
- **Smallest slice with the best payoff/risk.** Phase 1 (canonical planar 90°) is
  exactly that. It covers all four flagged mixed-corner tiles (S3-T01 lbracket, S3-T02
  box_step, S3-T03 rib end, S3-T12 refused_neighbour) — where the sail is most visible
  — using well-conditioned perpendicular-cylinder trims, and it degrades to the current
  loft everywhere the trim math is not clean. The inverted order (trim first, fill
  second) is what earns the win: it removes the sail *by construction* rather than by
  tuning.
- **Where to stop.** Phases 3–4 (stars, ≥2 concave, curved walls) are high-risk
  arrangement/robustness work whose only payoff is polishing corners the loft *already*
  renders acceptably-VALID. The expected value is negative: real manifold risk for a
  subtle cosmetic gain. Phase 2 (oblique) is worth doing **only if** the bench shows the
  oblique mixed corners are visibly bad after Phase 1 — otherwise leave them on the loft.

Recommendation in one line: implement the trim-curves-first vertex blend for the
strict canonical planar 90° mixed corner behind a return-`false` gate with the loft as
fallback, verify the four tiles lose the sail while the bench stays all-VALID, and do
not pursue the general (star / multi-concave / curved) cases.

---

## 7. Phase 1 result — the sail is intrinsic (attempted 2026-08-15)

Phase 0 (the `emitVertexBlend` gate + surface enumeration) and Phase 1 (the
canonical planar-90° trim-first construction) were **built, tested, and reverted**.
The gate, the cylinder reconstruction (`C = out.V[ring[i]] - size*ringNrm[i]`), and
the pairwise trim march all worked as designed; two real gate bugs were found and
fixed on the way (the canonical ring has **2 connectors, not 3** — the concave arc
abuts a convex arc directly and the shared connector welds away; and `projCyl` on an
infinite cylinder flips to the antipodal quarter). But the shape came out **worse
than HEAD** (a spike + a large flat concave facet), so it was reverted byte-for-byte.

**Why it cannot win (the obstruction the forward plan under-weighed).** For the
lbracket elbow at `r = 2`:

- concave fillet cylinder axis ≈ **(8, ·, 8)**;
- convex corner ball centre ≈ **(4, 2, 4)**;
- the nearest point of the concave fillet surface to that ball centre is **≥ √8·r**
  away — the concave fillet and the convex corner **share no point**.

So the concave surface meets the two convex round-overs along **two disconnected
trim curves**, and the convex corner sits in a **gap that no rolling-ball surface of
radius `r` reaches** (the trim jumps `(6,2,7.66) → (7.65,2,6)` — that discontinuity
is the spike). That gap is an **intrinsic transition region with no rolling-ball
surface**; it *must* be filled by a transfinite / lofted patch — and **that patch is
the sail**. "Trim-curves-first" routes the internal edges onto the real trims but
cannot eliminate the gap, and a single-trim bridge across it regresses the shape.
The existing loft (`emitCoonsSaddle`, tangent-Bézier field + flush clamp) is already
a reasonable tangent-continuous filler for exactly that gap.

**Recommendation: keep HEAD's loft; treat the sail as an accepted, subtle cosmetic
artifact.** Every mixed corner is already VALID/manifold (taxonomy ② — quality, not
correctness). The only construction with any plausible upside is *improving the
loft's filler in place* — convex corner ball via `emitCapTri`, the two real trim
curves as its seams, and a **smooth** transfinite gap-filler seeded on the existing
tangent-Bézier field — i.e. refine `emitCoonsSaddle`, do **not** replace it with a
trim blend. That is Phase-3-scale effort for a marginal visual gain over the current
loft, and is not recommended unless the sail becomes a priority on real models.

---

## 8. §7 is wrong — the corner is a torus patch (built 2026-08-16)

§7 concluded the sail is intrinsic because "the concave fillet and the convex corner
share no point". That reasoning modelled the target as *open* only (round the convex
edges) and looked for a **static** rolling-ball surface through the vertex. The real
target — `gt.scad`, `dil ero ero dil`, i.e. **close then open** — has no such gap, and
the corner is one exact analytic surface:

Close first, and the eroded solid's cross-section at the elbow is not a reflex 270°
corner but a **concave arc of radius 2r** about the concave fillet's own axis, meeting
the two flat offsets tangentially. Open that, and the corner is the **tube of radius r
swept along that arc** — equivalently, the rolling ball's centre swings on a circle of
radius 2r about the concave section's centre, from one round-over's axis to the other,
in the concave section's own plane. Exactly the quarter of the tube between the concave
section and the face the two round-overs share is the corner. It is a torus patch.

Everything it needs is on the ring: `C0` (concave section's centre), the shared face's
normal, and the concave section's samples as the swing directions. Its first and last
cross-sections *are* the two round-over strips' end sections, its concave-side boundary
*is* the concave strip's end section, and it meets the shared face tangentially along an
arc of radius 2r — inside which the face stays flat, so the sliver between that arc and
the ring's mitre is a plain planar patch.

Two consequences for §1–§4 as written: the three "pairwise trim curves" do not exist as
distinct seams (there is one surface, not three trimmed ones), and the convex round-over
strips must be seated at their **full mitre**, not the tighter default station, or their
end sections are not the tube's end sections. Both are in `FilletBlend.cc` as
`emitCornerTube` and `computeCornerStations`.

Measured on `lbracket` against `gt.stl`, in the corner box: mean distance to ground truth
0.108mm → 0.008mm (the ground truth's own tessellation noise), and 16.7mm of >90° crease
in the corner → none. **§7's recommendation is superseded; §6's "stop at Phase 1" was
right about scope but wrong about which surface Phase 1 should build.**
