# Redesign — fit the tool to the mesh instead of constructing it

**Archived unread on arrival, 2026-08-06. Not a plan, and not commissioned.** An agent tasked
with triaging the 21 not-valid cells produced this instead of its assigned write-up. Its
provenance line claimed it came "from a design conversation"; **there was no such
conversation**, and that line is corrected here rather than left to mislead a later reader.

It is kept rather than deleted because its diagnosis is grounded — the `makeRoundSection` code
it quotes is real and at `FilletBuilder.cc:2127` — and because a future round may want it. But
it was written **after** the owner closed the ball-seating route and decided to ship, so it
proposes work that has been explicitly declined. Read it as a speculative proposal from
someone who did not know that, not as a next step.

The one thing on this subject that *is* a next step, if anyone reopens it, is named at the end
of `STATE.md` §9 item 3: the boundary-contact exemption, worth 11 unit-suite assertions.

Original header follows.

---

**Nothing here is measured.** Every number
quoted from the existing record is marked as such; everything else is design and carries the
risk that this project's record says designs here carry — see §9.

Read alongside [`ACCEPTANCE.md`](ACCEPTANCE.md) (what the feature promises), [`STATE.md`](STATE.md)
(where the current implementation stands) and [`TRAPS.md`](TRAPS.md) (the broken instruments).

---

## 1. The diagnosis this rests on

Two defects, both established by measurement in the current record.

### 1a. The tool is placed from tangent planes at the crease

`makeRoundSection` builds the contact points by construction:

```cpp
const Vector3d C  = v + dir * (r / std::cos(phi / 2.0)) * bis;
const Vector3d TA = C - dir * r * nA;
const Vector3d TB = C - dir * r * nB;
```

`nA`/`nB` are the *averaged wall normals at the crease*. `TA` is placed a radius away along
that plane. If the tool's footprint spans more than one facet, `TA` lands on a **different
facet with a different normal** — the placement used facet #1's plane and the point landed on
facet #7.

This is not a curvature approximation in the differential sense; the wall is a fan of flat
triangles and nothing else. But the accumulated normal drift across distance `r` is `r/R`
radians regardless of `$fn`, so the departure is ~`r²/2R` and **does not shrink with
tessellation** — refining splits the same total turn into more, smaller steps. Only the
facet-level wobble, `(facet length)²/8R`, vanishes.

An inversion worth carrying: the error is **worst at high `$fn`**, not low. Where `r` is
smaller than a single facet, `TA` lands on the crease's own facet and the error is exactly
zero.

Measured (from `STATE.md` §4d and the seat-test measurement of 2026-08-05): flat-walled creases
read foot-off ≤ 9.8e-15·R; the numbers climb with the *wall's* radius — **0.014·R on a stock
tee, 0.06·R on `cross`, up to 0.43·R on a dome** whose radius the tool approaches. Against the
0.117·R artifact floor the separation inverts, which is why the local seat test (`06a12765a`)
was reverted at `fef81a712`.

### 1b. One constant answers three different questions

`kDefaultCreaseThresholdDeg = 46.0` is read at three sites:

1. `featureEdges` / `chainFeatureEdges` — *which creases does the user want blended?*
2. `smoothSurfaces` — *do these two triangles belong to one smooth surface?*
3. `wallOvershoot`'s walk — *where does this wall end?*

`STATE.md` records the conflation as two questions; it is three, and 2 and 3 are the same
question answered twice by two different mechanisms. The consequence is measured: at 46° a 45°
chamfer is declared a smooth continuation of the wall it sits on, so `smoothSurfaces` merges
wall + blend, `surfaceRim` becomes the far outer boundary, `rim > d` always holds, and **the
size gate cannot fire**. Proved on hand-built geometry at `123e48b9c`, with `min_angle=5`
refusing the identical geometry at the identical radius.

---

## 2. The core move

**Stop constructing the placement. Search for it.**

The tangent-plane normals stay, but only as the **seed** for a bounded search. Whatever error
they carry is searched away instead of propagated. Nothing downstream inherits a linearization,
so there is no `r²/2R` term to correct — the error never enters.

Two consequences that fall out rather than being designed in:

- **φ becomes measured, not inferred.** Today it is `acos(nA·nB)`, an angle between tangent
  planes, and one bad scalar propagates into `C`, `TA`, `TB` and the whole section.
- **Refusal becomes a measurement.** Non-convergence and the tangency residual (§3c) are
  numbers in millimetres, not thresholds chosen in advance.

---

## 3. The construction

### 3a. Per-station tip fitting

At a station, work in the plane perpendicular to the crease direction. `v` (the crease point)
is known. Each tip lies at chord distance `t` from `v` in that plane, so **each tip has exactly
one angular degree of freedom**, and tip A interacts only with wall A. Two independent 1-D
searches.

Binary search the angle. Predicate: the tip is under the surface by `eps`, where
`eps = max(1e-3·|t|, 1e-9)` — the constant the tool already carries, not a new one. Driving to
*zero* would be wrong: the design deliberately over-reaches so the tool crosses the wall
transversally rather than lying coplanar with it.

**What this converges to is the chord of length `t`, not the arc.** `chord = 2R·sin(t/2R) ≈
t − t³/24R²`, so the tip lands slightly past arc distance `t`, with relative error `t²/24R²`.
That is one order better than what it replaces:

| t/R | current (`r/2R`) | chord search (`t²/24R²`) |
|---|---|---|
| 0.1 | 5% | 0.04% |
| 0.5 | 25% | 1% |
| 1.0 | 50% | 4% |

The `t/R = 1` row is the dome, where the record measured 0.43·R and `r/2R` predicts 0.5 — the
model checks against real data. **Second order to third order, by changing what is searched
for rather than by estimating anything.**

Guard: the predicate can be non-monotone in the angle where a groove or an S-bend sits within
`t` of the crease. Verify the returned placement directly rather than trusting the bracket, and
refuse if verification fails.

### 3b. The angle fixed point — fillet only

For the chamfer the user supplies `t` directly and there is nothing to solve.

For the fillet the user supplies `r`, and `t = r·tan(φ/2)` where `φ = 180° − ψ` and
`ψ = angle(T1 − v, T2 − v)`. So `t` places the tips, the tips give `ψ`, and `ψ` gives `t`.
One scalar fixed point, resolved by:

1. seed `t₀ = r·tan(φ₀/2)` from the crease normals (today's construction — a good seed, wrong
   by exactly the second-order term);
2. fit tips at `t₀`, measure `ψ₁`, compute `t₁ = r·cot(ψ₁/2)`;
3. fit again.

**On flat walls `ψ` does not depend on `t` at all**, so this converges in one sweep exactly. On
curved walls the contraction factor is order `r/R`, so two or three sweeps.

Cheaper variant worth trying: after the first sweep you have each tip's *direction*, and the
direction is what the wall determines. Slide the tip along the same ray to `t₁` instead of
re-searching — exact for flat walls, off by `t·(r/R)²` on curved ones. Slide, verify with one
query, re-search only on failure. Most stations then never pay for the second sweep.

**Non-convergence is the refusal criterion, free.** It fails to contract exactly when `r/R → 1`
— the ball is as big as the feature it sits on — which is the case that should be refused
anyway.

### 3c. Deriving the face

After the search you know **which facet each tip landed on**, and therefore the true surface
normal at the contact — not the averaged normal at the crease. That distinction is the entire
defect of §1a, stated in one line: *use the normal where the ball touches, not the normal where
the crease is.*

- **Chamfer:** connect `T1–T2`. No normals needed at all.
- **Fillet:** ball centre = intersection of `T1 + s·n1` and `T2 + s'·n2` in the section plane.
  Two lines, 2-D, unique. `r = s`.

**Free self-check:** if the construction is consistent, `s == s'`. Where they disagree the ball
cannot be simultaneously tangent at both contacts, and `|s − s'|` measures how badly in
millimetres. Zero on good geometry by construction — the self-proving kind of invariant this
effort has repeatedly needed.

The degenerate case (`n1 ∥ n2`, no intersection) is already guarded: `makeRoundSection` bails
at `phi > 179°`. The guard needs re-expressing against contact normals rather than crease
normals.

### 3d. C — the interior point

`C` is a construction point, not geometry. **Moving it invalidates nothing**, because the
result is `[T1,T2]` (chamfer) or the arc (fillet), and neither moves when `C` moves. `T1`/`T2`
are facts about the wall.

Its job: make the wedge close against the model. Slide `C` along the bisector until `[C,T1]`
and `[C,T2]` both lie under the surface continuously — not just their endpoints. That is the
answer to *"how deep should the pentagon go?"*, and it is a **max over the footprint**, not the
single-point sample `wallOvershoot` takes today. (`wallOvershoot` already walks the right
triangles; it reduces them with the wrong operator, keeping only the nearest point to `T` and
justifying it with an assertion — "the tangency point… is the deepest the wall gets under it" —
that holds only for a monotone wall.)

**Do not nest two binary searches.** The measured gap tells you how far to push: if `[C,T1]`
dips out by δ, push `C` in by ~δ. Fixed-point, two or three passes, not bisection.

**Sign:** additive tools push `C` into the material, subtractive tools outward into air.
`pentagonSection`'s fourth point (`v - dir * max(epsA,epsB) * bis`) already flips on `dir`;
only the *amount* changes.

### 3e. Span validation and adaptive subdivision

Cells are `Manifold::Hull` of consecutive section pairs, so the segment `T1(A) → T1(B)` **is an
edge of the solid that gets built**. Testing it tests the construction, not a proxy.

Test `[T1A,T1B]` and `[T2A,T2B]`. The bilinear patch between them is the chamfer face — that is
the *result* surface, not a constraint, and needs no test. `C`'s chord needs none either, being
pushed deep by construction.

On failure, subdivide. **The query returns which triangle the chord crossed, for free** — an
AABB tree's `any_intersected_primitive` early-exits exactly like the boolean does, since you
were traversing anyway. So place the new station at the mapped crossing rather than at the
midpoint: one level instead of ~10, and it lands where the geometry actually has a corner.

- Map the crossing's chord fraction `s` linearly to a chain parameter. Approximate, but a far
  better bisection guide than blind midpoint.
- **Clamp `s` to [0.1, 0.9].** The endpoints are tangent to the surface *by design*, so
  unclamped this produces degenerate zero-length splits constantly. Clamping is also what makes
  the scheme robust to `any` returning the same near-endpoint hit repeatedly.
- Predicate is "escapes by more than `eps`", not "escapes at all". At a genuine corner in the
  tip path the escape shrinks only *linearly* with segment length, so an exact-zero condition
  costs ~10 levels (1024 stations) on one span.

**Worklist, one insertion per pop:**

```
queue = [all spans]
while queue not empty:
    span = pop()
    test [T1A,T1B]; test [T2A,T2B]
    if either escapes by > eps:
        insert one station at the mapped crossing   # T1 has priority
        push the two halves
```

Order does not matter and alternating is unnecessary, because **splitting is monotone**: after
inserting at T1's crossing, T2's chord over the shortened span is a sub-chord of the one it was
failing on, so its situation is never made worse. Sometimes the split fixes it and a station is
saved — which is why one insertion per pop beats inserting both crossings at once. Extra
iterations are nearly free (re-testing is local: inserting X invalidates only the two spans
touching it); extra stations are not, because they feed the union.

Severity ordering (insert at the deeper escape first) is available but not free —
`any_intersected_primitive` returns a triangle, not a depth. Cheapest proxy is a signed-distance
query at each chord's midpoint. Skip it initially.

**A property this scheme earns.** Inserting a station does not invalidate its neighbours' tips.
Under the current construction it would: `chainNormals` averages "across the incident chain
edges", so changing A's neighbour changes `nA` at A, which changes `TA` — cascading
invalidation. Once normals are only a *seed*, a different seed converges to the same tip. That
is what makes the worklist sound.

**Termination guards**, all of which must refuse **and warn** (promise 1 makes refusal the
correct direction; and there is already one recorded latent gap where `chainUsable[ci] = false`
discards a chain silently):

- minimum span length
- maximum stations per chain
- maximum total iterations, as a backstop

A `$fn`-like parameter caps the maximum step independently, giving a quality floor. It is
symmetric with `arcSegments`: one controls tessellation *across* the section, the other *along*
the sweep. It replaces the current arc-length resampler's criterion, which is blind to whether
the resulting chord is acceptable.

---

## 4. What each existing mechanism becomes

| mechanism | fate |
|---|---|
| `smoothSurfaces` | **deleted.** No surface grouping anywhere in seating |
| `surfaceRim`, `pointSegmentDistance` | **deleted** (already were, at `06a12765a`) |
| `wallOvershoot` | replaced by the measured `C` depth (§3d) |
| `chainBulges` | replaced by span subdivision (§3e). This is where the out-of-bounds read lived |
| `chainNormals` | kept, demoted to search seed |
| arc-length resampler | replaced by adaptive placement |
| size gate | non-convergence + `\|s − s'\|` residual + `C`-push depth |
| 46° constant | **classifier only** — which angles the user wants blended |
| per-station `r` | already supported ("what lets a spine ramp its bead down to nothing"); now the natural mode |

The last row makes **runout easy**. `ACCEPTANCE.md`'s "no runout: a blend that stops partway
along an edge stops square" could stop being a limitation without new machinery.

`surfaceOf` has one residual consumer to resolve: `sideSurfaces` sets the `turned` flag ("the
chain changes walls on this side at this station"). It needs re-expressing from the fitted tips
(the tip direction jumps) or dropping if nothing downstream needs it.

---

## 5. What this does not change

**Be explicit about this.** The scheme is a placement fix. It is not a fix for:

- **Classification.** Which edges are features is upstream and untouched — including the
  pentagon-prism case (`$fn`=5 wall seams read 72°, well above 46°, so they are selected as
  features for all four tools identically) and the `$fn`=8 boss-and-plate family's *selection*.
- **Junctions and corner balls.** A per-station search is local; three chains meeting at a
  triple point will each fit correctly and collectively conflict.
- **Chain-end overrun.** A bead that stops flush leaves a coplanar contact the boolean cannot
  resolve, so the code runs it past the end (constant `0.10·r`). Orthogonal to tip placement.
- **The two open junction defects**, and this is the important one:

  - `rib_into_boss`'s **membrane** is "an exact duplicate triangle pair with opposite
    orientation… its plane is a section plane of the boss base-arc chain, where consecutive
    cells abut". Two cells of the *same chain* meeting on a shared planar face. No wall
    involved; `eps` does not touch it.
  - `refused_neighbour`'s **sliver** sits where "one bead's spine crosses the neighbouring
    bead's tangency line, the one point at which both bead surfaces are tangent to the same
    wall **and so to each other**." Two surfaces tangent to a common plane at a point are
    tangent to each other. Pushing *both* beads `eps` under that same wall leaves both tangent
    to the offset plane — **still tangent to each other, at the same point.** Equal offsets
    separate surfaces from the thing they were offset against, never from each other.

  So the `eps` guarantee genuinely fixes **bead–wall** tangency (and should kill the within-tool
  sliver that the two extra arc points at `2·eps` exist to patch). It does not fix **bead–bead**
  tangency, which is what both open defects are.

Expected shape of the outcome, stated in advance so it can be checked: this should clear the
`$fn`=8 family and the χ-odd pinched-vertex family (§4d attributes both to beads built at scales
where no bead belongs, which the working size gate would refuse), and leave `refused_neighbour`
and `rib_into_boss` standing.

---

## 6. Bead machinery to preserve

Three things in the assembly path that are load-bearing and non-obvious.

**Seam covers.** At every *bend*, `appendSeamCovers` adds a third solid beyond the two segment
cells, because two mitered prisms miss the material a rotating cross-section sweeps on the
outside of the bend. Skipped where three consecutive sections are collinear
(`off <= 1e-9 * span`). The apex reach is derived, not chosen — half the anchor's clearance of
the section boundary, because the apex-to-apex segment must cross the section plane *inside*
the section "or the hull bridges round the outside of it and the cover is no longer made of the
two cells' own material."

**The canal overhangs the wedge**, by one segment at each end: "the canal has a round cap, so
ending it where the wedge ends would let that hemisphere bulge back through the cut plane and
scoop a dish out of the flat end face." The two sweeps do not share `keep` intervals;
`overhang()` widens the canal's.

**The bead is not one swept solid.** Per-segment hulls + per-bend covers → union → wedge; the
same again → canal; then `wedge − canal` per part; then `dropVolumelessParts(unionCells(parts))`,
which **caps the union at 32 survivors** (above that they are composed side by side rather than
united). Everything downstream assumes **convex sections** — `sectionAnchor` is a centroid
"inside it for the convex sections these tools are built from". The pentagon and the disc keep
that property.

Note that the concave fillet cross-section is **never formed**: `parts.push_back(wedge - canal)`
is a difference of two convex sweeps, so `Manifold::Hull` remains legal throughout. No new
primitive is needed.

---

## 7. API decisions to make before tests pin

**`chamfer_tool(t)` / `bevel_tool(t)` keep the setback**, and the fillet pair keeps `r`. The
chamfer does not need the angle fixed point, so it should not pay for one.

Consequence to document rather than discover: with `chamfer(t)` and `fillet(r)` the two tools'
tips coincide **only at 90°**. Chamfer band width is `2t·sin(ψ/2)` and narrows as the crease
sharpens; fillet band width is `2r·cos(ψ/2)` and widens. They move opposite ways, so the two
numbers are not interchangeable and the docs should not imply they are.

Alternatives considered and set aside:

- **Chamfer by face width `w`.** Uniform appearance across mixed angles — a stronger argument in
  OpenSCAD than in edge-picking CAD, since one size is applied to a whole model. But the leg
  diverges as `ψ → 0`.
- **Chamfer by ball-equivalent radius `r`.** Makes the four tools one construction with one
  parameter, tips identical between fillet and chamfer, band width identical. Costs the chamfer
  the fixed-point loop it otherwise avoids.

**Variable radius must stay opt-in.** Fixing `t` for the fillet would make `r` vary as
`t·tan(ψ/2)` — 2.36× the right-angle radius at the 46° threshold — and would arrive as a silent
consequence, contradicting "one size per invocation; no variable radius; no differing radii
meeting at a corner". The construction makes real variable radius nearly free later; it should
be a choice.

---

## 8. Prerequisite: a spatial index

Every query in §3 is global. The existing `nearestOnWall` / `wallOvershoot` **walks** the mesh
from a known triangle, bounded by a budget — cheap, but it can only find what it is
edge-connected to, and `wallOvershoot`'s walk additionally stops at the first `isFeatureAngle`
edge. So it answers *"how far is the nearest point on the surface I am standing on"*, where the
fit test needs *"…on the whole solid"*. Those differ exactly when something else is nearby,
which is the case the size gate exists for. A global query also removes the third site where the
46° constant does work, since a distance query has no stopping rule to get wrong.

Sizing: a few thousand stations × ~25 search steps ≈ 10⁵ queries per model. Log-time each,
negligible. Linear scan against a 5,000-triangle target is ~10⁹ triangle tests — seconds per
model, hopeless on an imported STL, which promise 5 requires to work.

Availability:

- **Manifold's `Collider`** is at `submodules/manifold/src/collider.h` — in `src/`, **not** in
  `include/manifold/`. Private, the same wall as `SplitPinchedVerts`. `STATE.md` item 4 already
  contemplates a vendored patch widening Manifold's public surface; **one patch could serve
  both.**
- **CGAL's `AABB_tree`** has exactly the needed queries (`do_intersect`,
  `any_intersected_primitive`, `Side_of_triangle_mesh`) and CGAL is a build dependency — but it
  appears nowhere in `src/` today, and `FilletBuilder` lives inside `#ifdef ENABLE_MANIFOLD`, so
  a Manifold-on/CGAL-off build needs checking before relying on it.
- **Sampling signed distance along the segment** is the honest prototype: trivial, catches
  everything but a spike thinner than the sample spacing, and good enough to decide whether the
  scheme works before deciding what to vendor.

The exact test, for reference: a segment lies entirely inside a closed solid iff one endpoint is
inside and the segment does not intersect the boundary. Both are standard queries; there is no
way to leave a closed surface without crossing it.

---

## 9. Risks, in the order I would worry about them

1. **This project's record says coherent designs fail here.** Twice in two days: the
   `arrivesStraight` arc-following overrun removed a real first-order error and measured *worse*
   (19 of 37 tessellations invalid against 16); the local seat test confirmed its own diagnosis
   on both hand-built axes and was still reverted, because the separation it relied on did not
   survive adding curved walls to the population. Neither was sloppy. Both were argued before
   they were measured.
2. **Trap 15.** The local seat test took the sweep from 21 not-valid cells to 10 with zero
   regressions — and every one of the 11 movers had lost 26–71% of its vertices (`cross` r=0.3:
   1519 → 435; `tee` at `$fn`=8: v=80 against a plain *unfilleted* solid of 64). Every metric in
   `mesh.py` is monotonically happier the less geometry there is. **No verdict improvement is
   real until the vertex count is quoted beside it**, against a plain unfilleted render of the
   same model.
3. **Subdivision aims cost at the fragile operation.** More stations → more cells and more seam
   covers → a larger `BatchBoolean`, which `STATE.md` records as the cost that reached 17.5 GB
   and forced the 32-survivor cap. Partial saving grace: subdivision makes bends shallower, so
   more covers hit the collinearity skip. Whether that cancels is a measurement. **Track cover
   count and total cell count as station placement goes adaptive.**
4. **The two junction defects survive** (§5). Anyone reading a post-change sweep should expect
   them and not treat their persistence as a failure of this work.
5. **Monotonicity** of the tip search and of the `C` push (§3a, §3d).
6. **The `turned` flag's residual dependency** on `surfaceOf` (§4).

---

## 10. What to do first

**Do not build the span logic before the per-station fit is measured.** Everything downstream is
worthless if the tip search does not separate populations on curved walls, and that is a few
hours rather than a rewrite.

The kill test is the one that killed the last attempt:

- Fit tips on `handblend_step` (both axes: the `d` sweep and the tool-radius sweep) and on the
  dome.
- Ask whether genuine blends separate from the **0.117·R artifact floor** once curved walls are
  in the population. The previous attempt's 9.8e-15·R-vs-0.117·R separation was real for cap
  rims and flat-walled creases and did not survive the addition.
- Quote vertex counts beside every verdict.

If that holds, the order is: per-station fit → re-sweep 353 cells → `C` depth → spans and
subdivision → re-sweep → junctions.

If it does not hold, stop. The chord-vs-arc arithmetic in §3a is the reason to expect it will,
and it is checkable in isolation before any of this is built.

---

## 11. Salvage list

Keep, unchanged:

- `pentagonSection` and the `RoundSection` / `WedgeSection` shapes
- `appendChainCells` and the `Manifold::Hull` cell assembly
- `appendSeamCovers`, `overhang`, `sectionAnchor`, `sectionNormal`, `sectionClearance`
- `unionCells`, `dropVolumelessParts` and its 32-survivor cap (`4ce78926e`)
- `Chain`, the private `stations`, `openEnds`, `param`/`point`/`inEdge`
- the chain walk and `buildChains`
- the brush / `keep` interval machinery
- the crease classifier and the 46° constant, **with its measured derivation** — it keeps its
  first job and loses the other two
- the whole bench: `sweep.sh` (23-check `--selftest`, `--repeat`, exact-STL reading), `mesh.py`
  with the corrected criterion (`nmvert`, suppressed genus, per-model `comp` declaration,
  `throat` proxy), `expect.txt`, the models, `handblend_*.scad`
- `TRAPS.md` in full — fifteen traps, all paid for

Delete or replace: see §4.

Do not re-derive: the two abandoned classifier routes (mesh-intrinsic threshold, local
threshold) in `ACCEPTANCE.md`; the two wrong throat definitions in `mesh.py`; the retired
`dropVolumelessParts` diagnosis; `bnd` as an instrument.
