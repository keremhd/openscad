# State of the fillet feature — 2026-08-04

A description of where the work stands, not a plan. Written to be forked from: it says what is
true, what is proven and by what evidence, and what is believed but unproven.

Branch `kerem-fillet`. **Nothing is pushed.** Suite **2230 assertions / 88 cases**.

The recorded pin of "1709 / 85" was stale for some time before it was noticed: the tree
measured 2224 / 86 by stash on 2026-08-04, so the figure the gate was written against had not
been true for a while. A pin nobody re-measures is a comment, not a check.

**This file is amended in place. Do not start a new dated state or handoff document** — the
dated chain grew 392 → 778 → 1421 lines and its cost is what retired it.

Read alongside it: [`ACCEPTANCE.md`](ACCEPTANCE.md), what the feature promises and the gate it
ships against — that is the authority on whether something is work or a release note; and
[`TRAPS.md`](TRAPS.md), the environment traps, the nine broken instruments and the corrections
to the record. Everything superseded is under `archive/` and stays in git regardless.

---

## 1. What is true of the code right now

**No environment variables.** All five are gone — `OPENSCAD_FILLET_LOCALGROUP`, `SEAMOVER`,
`SIZEGATE`, `RESAMPLE_DEBUG`, `SIZEGATE_DEBUG`. Each mechanism either ships unconditionally or was
deleted:

| mechanism | shipped state |
|---|---|
| local grouping (D17.3) | **on**, unconditionally |
| seam overrun | the constant **0.10·r** |
| `seamRoom` / retreat fallback | on (an earlier review's "delete three mechanisms" conclusion is recorded as wrong) |
| size gate blind-crease branch (D21) | **on** (`asksBlindCreases == true`) |
| `arrivesStraight` | **deleted 2026-08-04** — see §4 |
| resample and gate diagnostics | deleted |

**The crease threshold is a constant, `kDefaultCreaseThresholdDeg = 46.0`**
(`FilletBuilder_internal.h`). No render variable and no mesh statistic is read; `min_angle`
overrides it as before. `seamAngle()` and its clustering, `creaseThreshold()`, and
`buildFilletTool`'s `thresholdOverride` parameter are all gone, as is the workaround that
measured `fillet()`'s threshold once on the child and handed it to both passes — that was
C++-only privilege a SCAD author could not express, and `fillet()` is required to be pure
sugar over the four tool modules. `CurveDiscretizer::getMaxSeamAngle()` now has **no
production caller**; only a test helper uses it.

`SEAT` / `WALLFACE` never had a reader on this branch; D19 is parked at tag `d19-wall-recognition`.

**`Chain::verts` is now `Chain::stations`, and private.** The audit's patches are applied and R1/R2/R3
taken. The rename surfaced 58 references, of which **three were genuinely wrong** — a test walking
every station of a closed chain into `m.pos`; a seam-truncation loop indexing the station list with a
length from `sections[ci].size()` that is only equal inside that window; and `ChainContact::vert`
holding an interior station index, write-only, deleted.

**The end-vertex guarantee holds without asserts.** `endVert` is replaced by
`bool openEnds(int& front, int& back)`, which returns false for a closed chain. `endVert` has zero
occurrences left in `src/` or `tests/`. Eleven duplicated call-site `closed` guards were folded in
(nine entirely; two keep their `sections[ci].size()` half).

---

## 2. Evidence, and how strong each piece is

**Proven, independently reproduced:**
- Env-var removal is a faithful flag-flip: all 16 diff hunks are constant-folds of the gated-on path,
  verified by source at `-U0`, so equivalence holds for **every** model, not only those measured.
- 225-model corpus, both arms run twice: **4 real movers, none worse**, one better
  (`boss_on_plate_fn192`, non-manifold edges 88→40, genus −44→−20). Unsound count 7 of 225 before and
  after, same list.
- Census counts identical either side: 300 refusals, 498 blind chains, 0 blind refusals, 54
  re-divided. Shipped binaries select 7428 creases and refuse 6887 both sides. **The feature fires; it
  stopped asking permission.**
- The `stations` rename and the `openEnds` refactor are byte-identical on 19 models — the second
  re-taken independently after the first agent was killed twice, and stronger than md5: the entire
  console output matches, every `ECHO: fillet_tool:` count included.
- `openEnds` refuses where it must: a probe fired **1139 times, all `closed=1`**, 1135 from
  production code.

**Proven, single-source (believed, not reproduced):** F6's 109 ring closing edges (re-measured once by
a reviewer with its own probe, `grid100`=100 predicted in advance, `rib`=0).

**Unproven and known to be so:**
- The `size() < 2` half of the `openEnds` guard **never fired** (0 of 1139).
- No clean-`944e0cbef` baseline exists anywhere in this session's work — every comparison is
  pre-change vs post-change.
- Manifold backend only. No CGAL, no `$fn` or scale sweeps, no fold census, no D19 ledge, no D17.3
  oblique sweep.

---

## 3. Structural facts worth not rediscovering

- **`stations` privacy is what prevents the recurring fault**, not vigilance. An interior station
  index cannot be named outside the class; all 23 `endVert`-era sites sat behind a `closed` guard; all
  16 `stationCount()` sites are station-space bar one. A reviewer established there is no fourth
  instance, structurally rather than luckily.
- **The old `assert(!closed)` was dead twice over** — compiled out under `-DNDEBUG` *and* unreachable,
  because eleven call-site guards already refused. What kept rings out was the guards, never the
  assertion the header credited. The `openEnds` fix is therefore **prospective**, not a live-bug fix.
- **1135 of 1139 rings had `back >= 0`**, so the pre-fix accessor would have returned a plausible
  *wrong* vertex rather than a `-1`. Worse than the failure mode R1 and the old prose described.
- **The shell-level `fillet-tests` suite is largely blind to fillet geometry** — CSG scene-graph dumps
  plus fuzzy image compares on models the seam rule does not touch. `ctest -R fillet` passing is
  necessary, never sufficient. The four models that would prove it are slated for deletion before
  merge.

---

## 4. `arrivesStraight` — deleted 2026-08-04

Record: `decisions/2026-08-04-arrives-straight-goes.md`, which supersedes `…-stays.md`.

**Two things the earlier record got wrong, both found by instrumenting rather than reading.**

First, there is no seated ball at a curved arrival. The predicate was documented as handing
the vertex back to an older seated-ball construction; instrumented at `bcurve.scad`'s two
curved-arrival vertices, `chainJunctions` finds **no junction at either**, and it is
structural — the crease that leaves such a vertex unfilleted is one a brush cut, and the same
brush withholds the corner through `noCorner`. So the vertex fell to the plain
stop-a-hair-short branch, whose own comment already describes the outcome: both beads reach
the same point of the sharp edge tangent to the wall they share, meet at no angle, and the
boolean resolves that into a knife edge. **The fin was that knife edge**, and the predicate
was choosing between the overrun and nothing.

Second, the regression that justified keeping it does not reproduce. `box_step_fn24/48/96_r3`
are byte-identical with and without the predicate, χ=2, genus 0, zero non-manifold. `box_step`
is two cubes with no curved crease, so the predicate returns true at every end and cannot act
at all. The recorded genus 0→−2, −1→−4, −5→−15 was read through the `$fa`=12 threshold
artefact: misclassified tessellation seams manufactured curved arrivals the model does not
have. **Fixing the classifier dissolved the evidence, and the decision fell with it.**

`bcurve.scad` at `$fn`=64: χ 5→2, edges on >2 faces 5→0, genus −1.5→0. Swept `$fn` 8→80 in
steps of 2, **16 of 37 tessellations invalid before, 2 after**.

**Measured and rejected:** running the overrun along the arc through the last three spine
points rather than along the chord. It removes the stated first-order departure from the wall
and measures *worse* — 19 of 37 invalid, with tunnels at `$fn` 58/64/66. Past the seam vertex
the arc's continuation is buried inside the solid the *other* bead sits on, so following it
carries the overrun away from the bead it must overlap. The chord's outward drift is what
carries it there; the apparent defect was doing the work. Building the corner ball was not
tried and should not be — an unfilleted crease leaves these vertices, which is exactly when a
ball is wrong.

## 4a. The superseded record, kept because both framings were believed

Full record: `decisions/2026-08-04-arrives-straight-stays.md`,
`measurements/2026-08-04-arrives-straight.md`, `measurements/2026-08-04-arrives-straight-renders.md`.

**What it does.** At a seam corner an unfilleted crease also leaves, the seam rule builds no corner
ball and runs the two beads past the corner along the line their last segment lies on. That line is
the crease's continuation only if the crease arrives straight. On a curve the last segment is a chord,
and producing it past its end leaves the circle. `arrivesStraight` tests the last two segments for
collinearity and, where bent, hands the vertex back to the older seated-ball construction.

**It is kept.** Removing it degrades three corpus models and improves none
(`box_step_fn24/48/96_r3`, genus 0→−2, −1→−4, −5→−15), and the damage **grows with refinement**, which
by the owner's first standard makes it a real defect rather than faceting.

**But the branch it selects is invalid.** Rendered at a curved arrival on `bcurve.scad`: the fallback
leaves a fin of two sail surfaces meeting at a cusp, with **5 edges carried by >2 faces, Euler
χ = 5 (odd), genus −1.5**. A closed orientable surface can have neither. The other arm at the same
corner is χ=2, genus 0, clean. So the predicate **chooses between two bad constructions**; it is kept
because removal measured worse, not because what it selects is sound.

**Two framings that are recorded as wrong**, so they are not re-derived from scratch:
- The D17 review's cited win (`bcurve` 0/37) is the **"ungated + global"** arm — two changes. What
  shipped is the opposite half, local grouping made permanent. "Ungated + local" was never tabled.
- `arrivesStraight`'s original justification was one model, `rib.scad`, 9/37 → 1/37. It is now
  **inert on `rib.scad`** (all 37 tessellations byte-identical) although the census proves it fires
  there. It is kept for what it protects now, which is not what it was chosen for.

**Closed by owner decision, not by measurement:** whether local grouping deserved to be permanent.
Settling it needs a `removed + global subtraction` arm; global subtraction is broken and not worth
building, so a comparison against it has no value.

---

## 4b. The builder read one element before a vector — found and fixed 2026-08-05

This is the most consequential finding of the session, because of what it does to everything
measured before it.

`chainBulges()` mapped a chain parameter to a station segment with
`static_cast<int>(floor(q)) % nsta`. A section that overruns a seam vertex sits **outside** its
chain's parameter range — negative at the front end, put there deliberately by the runout with a
negative `back` — and C's `%` on a negative operand stays negative. So `chain.param(-1)` and
`chain.point(pos, -1)` read one element before the start of a `std::vector`.

`Vector3d` is 24 bytes, and every one of nine SIGBUS reports on disk faults at
`KERN_PROTECTION_FAILURE` **exactly 24 bytes below the start of a `MALLOC_SMALL` region**, in
`chainBulges`. Where the preceding page happened to be mapped, the read silently returned
whatever was there instead of faulting. **That is where every nondeterministic `rib_into_boss`
mesh came from** — not vertex-order noise, but a different geometry built from allocator
residue.

A probe on the release build confirmed it directly: two chains, `i = -1`, on every run.

There is a second, silent face of the same bug at the back end: the overrun puts `q` past the
last station, `(i+1) % nsta` wraps to station 0, and the chord is taken from the last station
back to the first — in range, so no fault, and geometrically meaningless.

**Fixed** at `cab639ffd` (cherry-picked from `fillet-chainbulges-oob`, `a4473e904`). An open
chain's segment index is clamped to its end segment and the fraction is left to run outside
[0,1], which extrapolates the segment the overrun section actually lies along; a closed chain
wraps with a floor-modulo. For any `q` in range both branches return the previous answer, so it
is a no-op except where the old code was out of bounds.

Proof, on the fix author's build: `$fn`=14, 60 identical runs, one mesh; `R`=1.0, 40 runs, one
mesh and no fault; `$fn` 10/12/26 and `R`=0.8, 20 runs each, one mesh each. Under UBSan with a
hardened libc++ the pre-fix build trapped 20 of 20 and the fixed build 0 of 25.

**What this costs the record: every measurement taken on a model with a seam vertex, before
this fix, is suspect.** The value used was whatever the allocator had left in the 24 bytes
before a station buffer. That includes an unknown share of the 225-model corpus.

**It does not explain everything.** `rib_into_boss` at `$fn`=14 is now deterministic and still
invalid — `v=222 e=660 f=442`, 3 non-manifold edges, χ=4, on 60 of 60 runs. Two surplus
triangles on three existing edges. The flake was hiding a real geometric defect, which now
reproduces on demand.

## 4c. The two changes measured together, 2026-08-05

`4ce78926e` (the `dropVolumelessParts` cap) and `cab639ffd` (the `chainBulges` fix) were built
and measured together for the first time on binary **md5 `11b6b3b1`**, Release + Manifold, no
source newer than it at the start or the end of the run.

**The builder is deterministic.** `rib_into_boss` at `$fn`=14: 60 identical meshes in 60 runs.
At `R`=1.0: 40 in 40, no SIGBUS. At `$fn`=12, the cell that used to produce no output: 20 in 20,
and it exports. Across the `$fn` axis at eight values, 8 runs each: one mesh per cell, against
two before. Across the whole 353-cell sweep, 1059 renders: `distinct`=1 on **every** row,
against 14 rows at 2.

**Sixteen of the seventeen recorded not-valid cells are real geometric defects.** The sweep was
re-run and diffed against the pre-fix table. 12 cells moved and **every one is
`rib_into_boss`** — the only bench model with a seam vertex, so the only place the read could
fire. Exactly one verdict changed: `$fn`=12, no-output → valid. 341 cells are identical in
verdict and in every count.

So the `$fn`=8 family, the four χ-odd cells, `cross` at r=0.3, `refused_neighbour` at four
radii, and `rib_into_boss` at `$fn` 14 and 32 are all geometry, not memory. `rib_into_boss` at
`$fn`=32 is **worse** than recorded once the garbage is gone: nonman 4 and χ=4, against nonman 2
and χ=3.

**The scope of the contamination is therefore narrower than §4b feared.** The warning there
stands for seam-vertex models, but 341 of 353 swept cells are unchanged by the fix, so prior
measurements on models without a seam vertex are not in doubt on this account.

**The unit-suite disagreement is settled, and both reports were right about their own tree.**
The suite is green at HEAD: 2230 assertions / 88 cases, six runs under six Catch2 seeds. The
three disputed failures are real at `c2acefa91` and `0c0727583` and gone at `4ce78926e` and
above — **the `dropVolumelessParts` cap fixed them**, and the out-of-bounds fix never touched
them. It is not the Catch2 comma trap; all three names pass individually when escaped. Note
what made the disagreement look impossible: **the identical 2230/88 totals in both reports were
not evidence they measured the same thing**, because the assertion count does not move across
the fix — only four outcomes do.

**Two instrument faults found in the sweep harness itself**, both corrected: `--selftest`
asserted that `rib_into_boss` must return more than one mesh, which was true when written and
false after the fix — inverted rather than deleted, so a returning read now fails the
instrument; and the end-of-run summary read the elapsed-seconds field as the binary column, so
it always claimed the table mixed builds.

## 4d. The debris is pinched vertices, and the round pass is what plants it — 2026-08-05

Four independent investigations converged on one mechanism. Each was asked a different
question; none knew the others' answers.

**The instrument is sound, and the week was not spent chasing an artifact.** `cross` at r=2.0
reads genus 4 identically from weld 1e-4 down to *unwelded*, and no welding occurs on that mesh
at all — the closest vertex pair is 5.8e-5 mm, sixty times the tolerance. Manifold independently
reports `Genus: 4`. The suspected mechanism does exist — two tetrahedra welded across a gap read
exactly `comp=2, nonman=0, χ=3` — it is simply not what these cells are doing. §5's "every
failing case reports Genus 0" is about the junction faults, not about `cross`; there is no
Manifold-versus-script disagreement here to adjudicate.

**But `mesh.py` produces false acceptances, which is worse.** It checks non-manifold *edges*
and never non-manifold *vertices*, and emits `genus` regardless. Five currently-green rows carry
debris: `tee` r=0.5 (`VALID χ=4 genus=1 comp=3` — two point-attached slivers and no tunnel at
all; the parity came out even because there were two pinches rather than one), `tee_small` at
**stock defaults** and at `$fn`=10 and r=1.5 (a fully detached 6-triangle fragment,
0.35 × 0.10 × 0.40 mm, sharing zero vertices — this is the `comp=2` §5 records as unexplained),
and `cross` r=0.9 (a detached 4-triangle shard, 3.45e-7 mm³, winding number 0, floating
*outside* the solid). `shallow_crease`'s nine `comp=2` rows are legitimate; that model renders
two plates by design.

**The χ-odd family is real**, and it is a 4-triangle tetrahedral sliver 0.05–0.4 mm across
attached at *exactly one vertex* — for `tee`, always at (1.913417162, −4.619397663, 8.086582838).
Tolerance-independent because those vertices are bit-identical. Manifold sees them perfectly
(`Genus: −1`, two shells); it just does not call two shells an error. `cross` r=0.3 belongs to
the other family — it carries `nonman=1` at every tolerance.

**`cross`'s genus is real and irrelevant.** The handles were isolated by ball-removal — eight
balls in the `x>0, z<0` octant take genus 4 → 0 with `comp` still 1 — and the throat measures
**1.5 µm**, about 130× below a 0.2 mm layer. Confirmed by the owner in a third-party slicer:
both r=1.5 and r=2.0 slice as a single object with no visible tunnel. They sit in *one* octant
of a solid with full octahedral symmetry, which alone proves them boolean noise rather than
intent. A correction to the record: an earlier reading of 0.18/0.42 mm was of near-*contact*
gaps, not throats.

**`Manifold::Simplify` clears twelve cells and breaks six**, so Part 1 cannot ship — but it
identified the mechanism. It clears the whole sliver family and, unhypothesised, the entire
`$fn`=8 boss-and-plate family at unchanged vertex counts. It breaks `tee_small` at stock
defaults, takes five χ-even cells to χ-odd with no warning, and takes the unit suite to four
failures — one of them a junction *gaining* a self-touch. It cannot be tuned: `Simplify(t)`
runs `SimplifyTopology()` unconditionally, every value from 1e-10 to 1e-5 yields an identical
mesh, and 1e-11 and below is inert. **It is a step, not a tolerance.**

**The convergent answer: the remnant is a pinched vertex, not a sliver of volume.** That is why
`SplitPinchedVerts()` fixes it and why vertex counts go *up*. The regression comes from
`CollapseShortEdges()`, whose own comment states it removes handles. Both are private and
Manifold's public surface exposes them only together. The next attempt is therefore
`CleanupTopology()` alone — `SplitPinchedVerts` + `DedupeEdges`, no edge collapse — reached
either by a vendored patch widening the public surface or by a pinch split written against
`MeshGL64`. `DedupeEdges` is separately the candidate for the membrane, which is an exact
duplicate triangle pair. `refused_neighbour` at r=1.05 says even that is not the whole family.

**And the round pass is what plants the debris.** `fillet()` is
`difference(union(target, fillet_tool), round_tool)`, the convex pass measured against the
already-blended solid. On `cross` the concave pass is *clean* — eight junctions, corner balls
seated, `noCorner=0`, `creaseLeavesUnfilleted=0`, **zero warnings and genus 0 at every radius**.
The convex pass then selects 52–68 "convex creases" that do not exist on the input model,
refuses about two-thirds of them, and subtracts beads along the rest; every warning coordinate
clusters at the eight triple points (±4.24, ±4.24, ±4.24). Material ends up cut back to radius
7.22 — *inside* the original sharp corner at 7.35, where the concave pass had pushed it out to
7.53. **The ragged notch at a junction is a subtraction, not a missing corner ball.** The corner
bead is built at all eight junctions, at every radius.

This retires a framing: §4's `arrivesStraight` reasoning is **inert on `cross`** — it never
fires. That conclusion was derived on `bcurve.scad`, where a selection brush cut the crease and
the same brush withheld the corner through `noCorner`. `cross` uses no brush. The scope of §4's
claim must be narrowed to brush-cut creases; it is neither vindicated nor refuted here.

### The steep ridges are bead–bead intersections, and ball seating separates them — 2026-08-05

Measured on `cross`'s concave-only output at r=0.5 and r=2.0. Probe on branch
`probe-convex-gate`, `7c9bf326e` (`FILLET_GATE_DUMP=1`). Dihedral instrument validated first:
a cube reads twelve edges at 90.000, a `$fn`=8 cylinder eight wall seams at 45.000 and sixteen
rim edges at 90.000, and the same cylinder rotated by half a facet returns an identical
distribution.

**There is no step at the tangency boundary, so the proud-bead hypothesis is refuted.** Of the
640 (r=0.5) and 606 (r=2.0) edges where a face in an original target plane meets a face that is
not — that set *is* the tangency boundary — **zero are convex**. Splitting all steep convex
edges three ways gives 114 with both faces in target planes, 72/91 with neither, and **0 with
exactly one**, which is the only bucket a bead standing proud of a wall could occupy. The bead
meets the wall exactly.

**They are intersection curves of two rolling-ball surfaces whose centres lie about one radius
apart.** Implied centre separation is 0.82–1.84·R at r=0.5 and 0.84–1.48·R at r=2.0 — linear in
r at coefficient ~1. That rules out the `eps` ladder (2e-3 mm against a measured 1.9 mm, 950×),
a constant epsilon (it moved 4× with r) and tessellation (the 19-gon sagitta is 0.082 mm and
fixed in r). Two spheres of radius R with centres d apart meet at `arccos(1 − d²/2R²)`:
0.82R gives 48°, 1.2R gives 74°, against measured dihedrals of 46–89° with median 51. The
arithmetic closes. Honestly reported by the agent: `|dist(vertex, implied centre) − R|` has a
median of 1.2e-3 but a maximum of 0.52, so a minority of ridge faces are planar corner-cell
facets rather than ball surfaces — the ~1R separation is a median statement, not exact.

**So the classifier and `round_tool` are both innocent.** A boolean union of two overlapping
beads is entitled to leave a real crease where they cross, and a classifier is right to see it.
The defect is that a tool asked for radius R builds beads against features it cannot seat on.

**Ball seating separates the two populations with thirteen orders of magnitude of daylight.**
Seat the ball from the two face normals, then check the constructed perpendicular foot on each
face actually lies on the mesh. Genuine features — `cross`'s 114 cap rims, `boss_plate`'s 38,
`boss_plate` at `$fn`=8's 20, `tee`'s 48 — read foot-off ≤ 9.8e-15·R, with **zero false
refusals**. Blend-made ridges read 0.117–1.004·R, **72 of 72 and 91 of 91 caught**. This is not
new machinery: it is a correction to a measurement `seatOn` already takes, which computes
`d = nearestOnWall(C, …)` and then never compares it to `r`, only to the rim distance. The
alternatives measure far worse — a ball-buried test alone catches 14/72 and 64/91, and a
relief-based cut leaves only a 1.46× gap between the blend-made maximum, 0.158·R, and
`boss_plate`'s genuine minimum, 0.231·R. **Ball seating is the rule to build.**

**The gate mostly never asks the question.** At r=0.5 the convex pass sees 68 chains and
accepts 26 — and **15 of those 26 have `ntest`=0**: every contact sample was exempted as a
chain end or as lying within `2·size` of a junction, so the fit question was deleted rather
than answered. At r=2.0 it is 8 of 24. The comment at `FilletBuilder.cc:1156` predicts this
exact failure, naming "a bead's runout lip, which arrives already broken into dozens of two-
and three-vertex chains."

**Scope:** `tee`, `boss_plate` and `boss_plate` at `$fn`=8 produce **zero** blend-made steep
convex edges. The signature needs a point where three or more beads meet; `cross`'s eight
triple points have it, a single junction or a lone foot ring does not.

### Root cause: the 46° threshold disables the size gate — 2026-08-05

Proved on hand-built geometry with no fillet module in the model, so nothing is entangled with
`fillet_tool`. Models committed at `f23d7652c`: `handblend_step.scad` (the measurement model,
with a built-in abundant-clearance control edge and a brush isolating one crease),
`handblend_controls.scad` (positive: cube corner; negative: a 0.1·R fin), and
`handblend_fillettool_ref.scad`. All three set `$fn` and are deliberately absent from
`expect.txt`, so `sheet.sh` ignores them and tile ids are unchanged.

**A tangent blend cannot reach the crease threshold, so the wall and the blend become one
surface.** `kDefaultCreaseThresholdDeg` is 46° (`FilletBuilder_internal.h:146`). The coarsest
tangent blend of a right angle is a single 45° chamfer, and 45 < 46. So `smoothSurfaces` unions
wall + blend + floor into a single surface; `seatOn` asks `nearestOnWall` for the contact point
and gets a point *on the blend*; it compares that against `surfaceRim`, which is now the far
outer boundary of the whole merged surface; `rim > d`, so it returns early and `offFace` stays
0. The edge fits. **The check cannot fire** — this is not a margin error.

One-parameter proof: at `min_angle=5`, below the blend's 7.5° facet so every facet is its own
surface, the *identical geometry at the identical radius* is refused, and the gate prints
exactly `0.934457 − d`. The default threshold accepts every d from 0.02 to 4.0; at d=0.02 it
accepts with 0.914 mm of wall missing, 91% of R. A second, independent axis agrees: sweeping
the tool radius at fixed d=0.2, ball seating predicts the limit at
`RT* = d + R·tan(Δ/2) = 0.26555`, and `min_angle=5` accepts ≤0.26 and refuses ≥0.27.

The geometric limit, derived independently rather than fitted: with the wall at x=0, the floor
at y=0, a blend arc of radius R tangent at (0,R) and (R,0), and the convex edge at (0,H) with
H=R+d, the seated ball's centre is (−R, d) and its wall contact is (0, d) — which lies on the
flat wall iff d ≥ R smooth, or d ≥ R(1−tan(Δ/2)) = 0.934457 as tessellated.

**One constant is answering two different questions, and that is the design defect. Owner
observation 2026-08-05.** `FilletBuilder.cc:1068` passes the same `thresholdDeg` to
`smoothSurfaces` that edge selection uses, so a single number decides both *"is this crease a
feature the user wants blended?"* and *"do these two faces belong to the same smooth surface?"*
Those want different values: the first is a statement about user intent, the second should be
near-tangency. At 46° the second assertion is that a 45° chamfer is a smooth continuation of
the wall it sits on, which is plainly false.

Corroboration that this was already felt: `FilletBuilder_test.cc:2176` and `:2185` call
`smoothSurfaces(cm, cAdj, 20.0)` with a hardcoded 20, not the threshold. The test author needed
a different value for segmentation and supplied one locally, because the production path offers
no way to.

Why it went unseen: both questions happen to need a value above 45°, and for the same reason —
grouping a `$fn`=8 cylinder's wall facets into one surface needs >45, and keeping those same
seams out of the feature set needs >45. One constant satisfied both and nothing complained.

**But splitting them and setting segmentation low does not work either**, and the reason is
already on record: a `$fn`=8 cylinder's wall seams are 45°, so at 10° the wall shatters into
eight surfaces and each face's extent becomes one facet, breaking the seat test from the other
side. That is Route 2's refutation in `ACCEPTANCE.md` — a cube, a `$fn`=4 prism and a `$fn`=8
cylinder are locally congruent, so no angle alone separates a coarse tessellation from a real
chamfer.

**Which suggests the seat test should not depend on surface grouping at all.** Ball seating
asks whether the constructed perpendicular foot lands on *the face the contact belongs to* — a
local question about one face, not about a merged surface. If that holds, the fix deletes the
dependency rather than retuning it, and the two-parameter split becomes unnecessary. **Not
verified.** If it does not hold, then two parameters are needed and the segmentation one must
be solved as the tessellation-versus-chamfer problem it is, not chosen as a number.

**This makes the rule easier to build than §4d implied.** Ball seating is *already implemented*
in `seatOn`; the defect is only that "the face's extent" is taken as the whole 46°-smooth
surface rather than the face the contact was meant to land on.

**And it strengthens the case against the arc/relief alternative.** The `offFace` distribution
here is 0.914457, 0.734457, 0.434457, 0.0344565, 0.00445653, 5.65e-05, 0 — linear in d and
*continuous through zero*. There is no gap to put a constant in; any cut misclassifies a band
of d its own width. Caveat from the agent: this is a straight extruded crease against a flat
wall, and a doubly-curved wall might separate the two rules differently.

**A scope correction to the section above.** `fillet_tool`'s blend surface is genuinely tangent
— on the same straight corner it reads 7.5° interior and 4.6232°/6.6268° at the boundary. So
the 46–91° ridges are **not** a property of the blend. On `cross` they are the bead's *runout
onto the cylinder near the triple points*, at min-axis radius 5.93–5.96, measured up to
103.797°. The finding stands; its scope is junctions, not blends.

**Instrument note.** The agent's own control caught its component counter reading `comp=19` on
a plain box — a Python chained-assignment bug corrupting union-find. Also: a plain rounded cube
carries 792 creases above 11.25°, **all** on triangles below 1e-4 area, so an area floor is
mandatory in any crease census here. **Not done:** `--repeat` on these cells, so trap 14's
flakiness check is outstanding; the results are structural and reproduce across two axes and
both brushed and unbrushed variants, but that is not the same thing.

**Why the second pass is structurally in trouble, and it is not the classifier's fault.**
`fillet()` uses one R for both passes, so the concave pass leaves surfaces already curved at R
and an R-radius rolling ball essentially cannot fit against them. The ~2/3 refusal rate is
therefore *expected and correct*; the suspect population is the 14–26 creases the size gate
says do fit. Two live hypotheses, under measurement at the time of writing: that the gate's
clearance test cannot see a neighbour curving away at exactly R, and that `fillet_tool`'s bead
sits *proud* of the wall by the deliberate `eps` offset §5 already names — a step of any height
has a steep dihedral, so a micron-high step would explain both the 46°–91° ridges and debris at
the micron scale, as one phenomenon rather than two.

### Ball seating made local: it fires, and it refuses curved walls — 2026-08-05

Built and measured, `06a12765a`. `seatOn`'s comparison against `surfaceRim` is replaced by the
constructed perpendicular foot measured against the wall, and `surfaceRim` and
`pointSegmentDistance` are deleted with it. The binary is md5 `f6d06ecb`.

**The root-cause diagnosis above is confirmed in full, on both of its axes and at the default
threshold.** On `handblend_step` with the test brush, the default 46° now refuses exactly the
set `min_angle=5` refused and accepts exactly what it accepted: D = 0.02, 0.2, 0.5, 0.9, 0.93,
0.9344 refused, D = 0.95, 1.0, 2.0, 4.0 accepted — the boundary at the independently derived
R(1−tan(Δ/2)) = 0.934457. Second axis, tool radius at D=0.2: accepts ≤0.26, refuses ≥0.27,
bracketing the predicted `RT* = 0.26555`. Unbrushed, the model's abundant-clearance control
edge is not refused at any D, and at D=2.0 nothing on the model is refused at all.

The reported miss differs from `min_angle=5`'s `0.934457 − d`, and the difference is
understood: with wall and blend still merged, the nearest mesh to the floating foot is the
blend, not the wall's end, so the number is the perpendicular distance to the arc,
sqrt(1+(1−d)²)−1 — measured 0.400071, 0.116034, 0.00449748, 7.3793e-06 at d = 0.02, 0.5, 0.9,
0.9344, against the closed form 0.400125, 0.118034, 0.004988 and the small-gap limit
sin(3.75°)·(0.934457−d) = 7.39e-06. The verdict boundary is unaffected.

**But the rule does not hold, and §4d's "Not verified" resolves NO.** The seated ball's centre
is constructed from the crease's two tangent planes. Where the wall curves in the direction the
tangency point is offset — a full radius along the wall — the constructed foot floats off the
mesh by about r²/2R for a genuine, comfortably-fitting blend. Measured, all on features that do
fit and that the previous binary blends without a warning:

| feature | r | foot miss | as a fraction of r |
|---|---|---|---|
| unit-test dome, R=8 sphere on a plate | 0.3 | 0.0074 | 0.025 |
| same | 1.0 | 0.0356 | 0.036 |
| same | 2.0 | 0.168 | 0.084 |
| same | 10.0 | 4.27 | 0.427 |
| `dome.scad`, R=12 | 1.5 | 0.00115 | 7.7e-4 |
| `tee.scad`, two d=10 cylinders, stock defaults | 1.0 | 0.0142 | 0.014 |
| `cross.scad`, concave pass | 1.2 | 0.0723 | 0.060 |

Against the artifact floor of 0.117·R that measurement recorded, the daylight is at best 1.4×
— the same gap the relief rule was rejected for — and it **inverts** once r approaches the
wall's own curvature radius. The thirteen orders of magnitude are a property of the populations
§4d measured, cap rims and creases on flat walls, and not of ball seating. `FilletBuilder.cc`'s
own comment above `seatOn` already stated this hazard as fact; it turns out to bound the whole
rule, not just the choice of contact point.

**The cost is total on cylinder-to-cylinder models.** `tee` at stock defaults now refuses 2 of
its 2 concave creases, `tee_small` and `tee_oblique` likewise across the whole `$fn` and radius
axes. Vertex counts collapse toward the unfilleted solid: `tee` at `$fn`=8 goes 220 → 80
against a plain union of 64, at defaults 224 → 160, `cross` 1239 → 435. The unit suite goes
from 88/88 to 85/88 — the dome case, the rib-with-a-bead case, and the boss/rib refusal count.

**So eleven of `sweep.sh --selftest`'s known answers now "fail" because their defect is gone,
and that is inertness, not repair.** `tee` r=0.5 and r=0.9, `tee_oblique` r=0.2/0.3/0.8,
`tee_small` at defaults / `$fn`=10 / r=1.5, `cross` r=0.9 and r=2.0 all come back clean —
because no bead is built on them any more. Standing rule: the debris and the fillet went
together. Do not read this as eleven cells fixed.

**The 353-cell sweep, three renders a cell, `distinct`=1 throughout**
(`results/sweep-f6d06ecb.tsv`, against the `fd3dec78` baseline). **Eleven cells go
INVALID→VALID and none goes the other way; the not-valid count falls 21 → 10.** The eleven are
`cross` r=0.3 and r=0.9, `tee` r=0.5 and r=0.9, `tee_oblique` r=0.2/0.3/0.8, `tee_small` at
defaults / `$fn`=10 / r=1.5, and `pipe_into_face` at `$fn`=8.

**Read the vertex counts beside them and it is not a fix.** Every one of the eleven loses
between 26% and 71% of its geometry — `cross` r=0.3 goes v=1519 → 435 — and across the sweep
166 cells lose vertices, up to 82%, while the cells carrying at least one warning rise from 140
to 201. The ten failures that remain are precisely the ones the change never touched: the
`$fn`=8 boss family, `refused_neighbour` ×4 and `rib_into_boss` at 14 and 32, all on models
whose every cell is byte-for-byte unchanged in vertex count and warning count. Thirteen models
are untouched entirely, and they are the flat-walled ones — `boss_plate`, `hole_plate`,
`lbracket`, `box_step`, `chamfer_box`, `pocket`, `thin_slab`, the controls. The damage falls
exactly where a wall curves: `tee`, `tee_small`, `tee_oblique`, `tee_large`, `cross`, `dome`,
`pipe_into_face`, `two_bosses`, `rib`, `mixed_fn`.

**What a successor has to do first.** The seat construction, not the seat test, is what fails on
a curved wall: the ball is placed from planes and then asked whether it touches a surface that
is not one. Any local fit test — the foot, or `d` against `r`, which is the same question since
d² = r² + off² on a flat wall — inherits that error. Re-seating the ball against the mesh
before asking is the prerequisite; until then the two-parameter split of §4d's last paragraph
is the live option, and it needs the tessellation-versus-chamfer problem solved.

### Seating the ball against the mesh: the rule is right and the grouping is the whole defect — 2026-08-05

Built on top of the local seat test, `2cd4f1431` and `5ec8d8fe1`, binary md5 `9eaf681d`.
Two changes, and both were needed:

- **The centre is re-seated against the mesh.** Newton on the two distances-to-mesh, held in
  the crease's own section plane by a third row, starting from the tangent-plane construction.
  It converges in **3 iterations** on every case dumped: at `handblend_step` D=0.02 the wall
  distance goes 1.22505572 → 1.00522588 → 1.000000, step 1.3e-16. Capped at 12 iterations and
  at half a radius per step, so it terminates deterministically whether or not it converges.
- **The residual became angular.** With the centre seated, the perpendicular foot *is* the
  contact, so stepping off along a normal measures nothing. What is left to ask is whether the
  direction from contact to centre is one the mesh calls "out" there: inside a triangle that is
  its normal alone, on a seam it is the whole fan of normals meeting on it. A wall that has run
  out has a one-sided fan and the direction falls outside it; the miss is what that angle
  subtends at the radius asked for.

**The normal cone is not optional, and the intermediate binary says so.** With the centre
re-seated but the foot still stepped off the contact triangle's own normal (`62224731`), the
false refusals got *worse* than the tangent-plane construction they were meant to cure — `tee`
0.0142 → **0.0253**, `dome` 7.7e-4 → **0.0243**, `cross` 0.0723. The cause is tessellation, not
curvature: the seated ball rests on a seam, and reading one of the two triangles that carry the
seam charges half the tessellation's own turn to the fit. On a `$fa`=12 sphere that is
r·sin(6°)·sin(12°), which is the 0.024 measured.

**Take the angles from a cross product, not from `acos`.** A seated ball's answer is zero and
`acos` loses half its bits approaching it: it read **1.5e-8 rad** on directions agreeing to the
last bit, which is above the `1e-9` margin the gate refuses at, and it refused `boss_plate` and
`dome` — flat-walled and previously untouched — at 2.98e-08 and 2.24e-08. `atan2(|a×b|, a·b)`
is exact there. This is a new instrument fault, of the kind TRAPS collects.

**Every curved-wall false refusal is gone.** The regression list from the previous attempt,
measured at stock defaults on the new binary, with vertex counts against the `fd3dec78`
baseline:

| model | before | now | vertices |
|---|---|---|---|
| `tee` | 2 of 2 concave creases refused, 0.0142·R | **0 refused** | 224, the baseline exactly |
| `dome` | refused, 7.7e-4·R | **0 refused** | 492 |
| `boss_plate` | (flat, never refused) | 0 refused | 474 |
| `cross` | concave pass 0.0723 | 26 of 42 refused, v up | 1290, against a 1239 baseline |
| `tee_small` | refused | 1 refusal left | 182 |
| `rib` | 16 of 36 refused | 12 of 36 | 484 |

Not one of these gained its verdict by losing geometry, which is what trap 15 exists to catch:
`tee` returns to its baseline vertex count exactly and `cross` comes back with *more* geometry
than the baseline, not less.

**And the rule reads the derived closed form exactly.** On `handblend_step` at `min_angle=5` —
the one-parameter control, where the blend's 7.5° facets are each their own surface so the wall
and the blend are not merged — the gate prints **0.914457, 0.434457, 5.6535e-05** at
D = 0.02, 0.5, 0.9344 and accepts D=0.95. Those are `0.934457 − d` to six figures: the
independently derived `R(1−tan(Δ/2)) − d`, not a proxy for it. The previous attempt's foot
measure printed the perpendicular distance to the arc instead (0.400071 at D=0.02); this one
prints the length of missing wall itself.

**But at the default 46° the gate is inert on that model at every D, and the reason is that
the seat is genuinely good.** All ten D values from 0.02 to 4.0 are accepted, D=0.02 included,
where 0.914 mm of wall is missing — 91% of R. The dump says why, and it is not a margin: the
re-seated ball reaches `d = r = 1` on both walls with a contact interior to a single triangle
and an angular residual of **8.9e-16**. The ball has rolled a full radius down off the flat wall
onto the hand-built blend and seated there *perfectly*, because a tangent blend merged into its
own wall is a smooth surface and a smooth surface of curvature radius R accepts a radius-R ball
anywhere on it.

**So this resolves §4d's open question in the opposite direction from the last attempt, and the
answer is better news.** The seat test made local does not need surface grouping to be *correct*
— the rule above has no `surfaceRim`, no dependence on how far a surface extends, and no false
refusals on any curved wall in the bench. It needs the grouping to be *right*, because the walk
that finds the contact may not leave the wall, and at 46° the wall it is given already contains
the blend. **There is no local geometric quantity that can separate these two populations**, and
this is the second construction to fail on it: the ball really does seat, so nothing measured at
the seat can say otherwise.

**The unit suite is 2230 assertions / 88 cases, 82 passed and 11 assertions failed**, unfiltered
on the pin. The previous attempt's canaries all clear: "a wall's own curvature is not an
overshoot" passes its four dome-Fits checks at r = 0.3, 1.0, 2.0 and 10.0, and the rib-with-a-
bead refusal counts are back. Six cases fail, in three families, and two of them are real:

- **A contact landing on its wall's own boundary now reads as a large miss.** "a contact landing
  on the edge of its wall is a fit, not a miss" — two overlapping bosses whose merged top face is
  bounded by the crease being blended — refuses one chain at 0.0627 and reads a worst miss of
  **0.9265 on a radius of 2**, where the old rule read 4.4e-16. "a bead the target already
  carries is not a wall in the way" fails the same way at 0.0640. The two rules define the
  boundary case oppositely on purpose: `surfaceRim` called nearest-point-equals-boundary a fit,
  because the blend then stops exactly where its wall does, while a normal cone at a one-sided
  boundary edge has no direction to accept and calls the same contact a miss. **The angular rule
  needs the crease's own edge exempted before it is usable**, and that is not the `turned`
  exemption, which fires only where the chain changes walls.
- **The reported magnitude is not monotone in the overshoot.** `r·sin θ` is bounded by r and
  turns over: on the perched dome the r=18 refusal reports 0.0124 against r=14's 0.0311, and
  "a blend wider than the face it must meet is refused" reports 4.685 where the test asserts the
  5.0 setback. The refusals themselves are all still there — it is the number in the warning
  that changed meaning, from a length of missing wall to an angle subtended at the radius.
- "a crease the exemptions cover is judged the same at any scale" fails with them.

**So it is not shipped.** The code is reverted for the same reason `06a12765a` was, and restored
the same way: `git revert 32344c0da 9bfe516c8 0867e9f45 d0f10f6c0` puts back the local seat
test, the mesh re-seat and the normal cone, in that order, and nothing else on the branch moves.
`src/` at the revert is byte-identical to `2f8fad57d`, and the binary rebuilt from it is md5
**`fd3dec78`** — the same binary the 21-cell baseline sweep was measured on, so no re-sweep is
needed to say the branch is unchanged. The unit suite at the revert is 2230/2230, 88/88.

**What that leaves.** The two-parameter split of §4d's last paragraph is now the only thing
between this rule and a working size gate, and the rule to pair it with exists, is measured, and
is committed. Its remaining dependency is `smoothSurfaces` — one call, one threshold — and the
question that threshold has to answer has narrowed to exactly one thing: is a 7.5° turn a
tessellation seam or a chamfer boundary. Route 2's refutation still stands against answering it
with an angle alone.

## 5. Open defects

| defect | state |
|---|---|
| seated-bead fallback at a curved arrival | **closed 2026-08-04.** See §4. |
| D22 — crease threshold reads render settings | **closed 2026-08-04** by replacing the derivation with the constant 46°. |
| `cross` yields no mesh at stock defaults | **closed 2026-08-04.** Not an empty mesh — a 17.5 GB OOM SIGKILL before the exporter ran. D22's tail, proven by a cliff at exactly 360/19, the model's own facet angle: `min_angle` 19 and above completes in 0.2 s and is valid, 18.9 and below is killed. |
| **unguarded union of surviving parts in `dropVolumelessParts`** | **closed 2026-08-05.** Capped at 32 survivors; above it they are composed side by side into one mesh instead of united. **The recorded diagnosis was wrong and is retired**: `Decompose()` was not the cost — forcing one on every call runs the repro in 32 MB and under a second — and a component-count cap still dies, at a union of 55 parts over 3505 vertices. The cost is the `BatchBoolean` over the survivors, which creates zero-measure contacts faster than the drop retires them and feeds a diverging mesh back into `unionCells`. The `sample` reading 1572 of 1572 in `Decompose` was measuring a mesh already grown huge by that feedback — a symptom read as the cause. The union cannot simply be removed: it welds contacts between survivors, and `selfTouching`/`Genus` in the junction tests read that welding, so removing it fails three cases. `cross` at `min_angle` 18.9→2 now completes in ≤1 s at ≤387 MB, `NoError`, genus 0. The largest union any bench model asks for is 14 parts, so on a sound model the cap is unreachable and the executed path is identical — by construction, not by measurement. |
| **`rib_into_boss` invalid at `$fn`=14 and 32** | **open, new 2026-08-04.** Same corner as the fin, smaller fault, on the bead surface where the two beads cross. **Reframed 2026-08-05, and the recorded framing retired**: this is not "invalid at 14 and 32". It fails at a scattering of values on either axis, the failing set moves when anything else changes, and it is nondeterministic run to run at every `$fn` tested. Re-derived on exact STL against a pinned binary: invalid at `$fn` **11, 25 and 32**, valid at 8, 19, 26 and 48, and **flaky at 14** — 6 valid to 2 invalid in 8 runs. The earlier "invalid at 14 and 32" was true when taken; the code has since moved. The remnant is an **exact duplicate triangle pair with opposite orientation**, a zero-thickness membrane, present in the `fillet_tool()` solid alone, so `buildRoundSolid` produces it rather than the caller's `union()`. Its plane is a section plane of the boss base-arc chain, where consecutive cells abut. |
| **the builder is nondeterministic in validity** | **closed 2026-08-05**, `cab639ffd` — an out-of-bounds read one element before a station vector. See §4b for the cause and §4c for the verification: 164 dedicated renders and a 1059-render sweep return one mesh per cell. |
| ~~the builder is nondeterministic in validity~~ (symptom record) | **superseded, kept for the evidence.** `rib_into_boss` at `$fn`=14: 2 of 40 identical runs of one unchanging binary returned a valid mesh (`v=226 e=672 f=448`), 38 returned invalid (`f=450`, nonman=3, χ=4), all rc=0. The topology moves, so this is not the known vertex-order noise. Independently reproduced on exact STL: 3 distinct md5s in 8 identical runs at `$fn`=11, 2 at 14, 3 at 25, 2 at 26, 3 at 32. See TRAPS 14 — it makes every single-render measurement on this branch, the 225-model corpus included, weaker than it reads. **`rib_into_boss` therefore cannot serve as an equality instrument for any before/after comparison.** `refused_neighbour` is deterministic and can. |
| **`rib_into_boss` SIGBUS at `R`=1.0** | **closed 2026-08-05**, same root cause and same fix — §4b. It was a read 24 bytes below a `MALLOC_SMALL` region, faulting only when the preceding page was unmapped. 40 runs clean after the fix. |
| D23 — size gate drops creases on impossible misses | diagnosed, unfixed. The "equal radius" framing is recorded as wrong. |
| D24 — bead truncated and left open at a refused neighbour | **closed 2026-08-04, does not reproduce.** Record: `decisions/2026-08-04-d24-does-not-reproduce.md`. Symptom is a blunt bead end, not a hole. |
| **`refused_neighbour` non-manifold at r = 0.2, 0.8, 0.9, 1.0** | **open, new 2026-08-04. Breaks promise 1.** A 0.34 µm sliver on 4 faces at r=0.9, stable across weld 1e-4…1e-9, on the concave bead's tangency boundary — the oblique junction, not the refusal. The bench carries r=0.5, which is valid. **2026-08-05, reproduced and extended.** This model is **fully deterministic** (3 of 3 and 6 of 6 identical exports), so the nondeterminism above is not involved. On exact STL against a pinned binary the original record reproduces exactly and gains two values: invalid at r = **0.2, 0.8, 0.9, 0.95, 1.0, 1.05**, valid at 0.3, 0.5, 1.2, 1.5, 2.0, and invalid across weld 1e-4…1e-12. A separate low-radius regime at r = 0.05 and 0.10 carries one warning and far more built geometry. The set is a scattering at every resolution probed: 0.7999 and 0.8 fail while 0.79999, 0.80001 and 0.8001 pass. The bad edge sits at x=0.440817, z=7.5 exactly, y=±(4−r)+δ — where **one bead's spine crosses the neighbouring bead's tangency line**, the one point at which both bead surfaces are tangent to the same wall and so to each other. A sliver wedge of area ~1.5e-7, not a duplicate triangle. Confirmed not the refusal: refusals sit at x=7.5. |
| **`$fn`=8 invalidates five boss-and-plate models** | **open, new 2026-08-05.** `boss_plate` (nonman=8, χ=6), `hole_plate` (8, χ=4), `two_bosses` (6, χ=5), `dome` (4, χ=4), `pipe_into_face` (2, χ=3) — every one of them valid at `$fn`≥10. Weld 1e-6, exact STL. A whole coarse-tessellation family, invisible to a single-point bench. |
| **χ-odd invalids carrying no non-manifold edge and no warning** | **open, new 2026-08-05.** `tee` at r=0.9 is χ=3 with `nonman=0` and **zero warnings**; also `tee_oblique` at r=0.2/0.3/0.8 and `cross` at r=0.3. Odd χ is the only signal, so neither the non-manifold count nor the warning count would catch these. **Breaks promise 1 silently**, which is the worst failure direction the gate names. |
| **`cross` gains genus with radius** | **open, new 2026-08-05.** genus 0 at r≤1.0, 2 at r=1.5, 4 at r=2.0 — all reported valid, because a genus-4 closed solid is valid. Whether a fillet may punch handles through the model is a promise question, not an A1 one. |
| latent A3 gap — `chainUsable[ci] = false` | **open, latent.** A two-station chain consumed by truncation is discarded with no warning; the node is argued not to reach it. |
| D19 — subtractive scalloped ledge | parked at tag `d19-wall-recognition` (`fe8c8d9d1`). |

### The two junction faults share a root cause, 2026-08-05

Both sit at a bead–bead crossing inside a junction, and **both are invisible to Manifold** —
every failing case reports `Status: NoError, Genus: 0`. OpenSCAD's vertex count exceeds the
exact-STL welded count by exactly the number of coincident pairs (359 against 358 at r=0.8; 562
against 560 at `$fn`=32): combinatorially manifold, geometrically pinched. An odd χ with
`nonman=0` is one such pinch seen from the other side.

The shared cause is the design decision to **separate coincident surfaces by a small offset and
let the boolean resolve the crossing**. That works generically and fails where the crossing is
tangential by construction — which is what a bead–bead junction is. The source already names
both failures in the past tense, at `cornerProfile` and at the seam overrun.

They differ in remnant: a **sliver wedge** on `refused_neighbour`, which vertex welding would
fix, and a **duplicate-triangle membrane** on `rib_into_boss`, which it cannot touch. So a fix
at the surface-separation level catches both; a mesh-cleanup fix catches only one.

**Proposed, not yet done.** Part 1: `Manifold::Simplify` / `SetTolerance` exist in the vendored
library and are never called in `FilletBuilder.cc`; applying `Simplify` to the tool at 1e-6·r —
three orders below the `eps`=1e-3·r ladder, so nothing deliberately built is in reach — should
clear the sliver family. Part 2, the membrane, is either deleting the opposed duplicate pair (it
bounds no volume, so removal is always safe, but re-stitching the fan is real work) or
guaranteeing an angle rather than an offset where consecutive cells abut, which is the
established remedy in this file but touches a construction every bench model depends on. Part 1
first.

**A leading indicator worth keeping**: `rib_into_boss` is exactly y-mirror-symmetric, and **13
of 33 tessellations export a y-asymmetric mesh** — far more than the invalid rows. At `$fn`=14
three of the eight unmatched vertices are precisely the non-manifold edge endpoints. Symmetry
breaking is visible where invalidity is not yet, which makes it the more sensitive gate.

**The corpus is blind to the family these belong to**: all 225 models write an explicit `$fn`, and
no pass has looked at a junction render. This session demonstrated the cost twice — the corpus
called the `arrivesStraight` branch clean and one rendered junction found a fin with odd Euler
characteristic inside it; and `box_step`'s recorded genus regressions turned out to be artefacts
of the same blind spot.

**The bench's two-axis blind spot is closed as an instrument, 2026-08-05.**
`fillet-bench/sweep.sh` walks a `$fn` axis and a radius axis, numbers only, checkpointing every
row; every bench model now carries a top-level `R` alongside `FNSET`, verified to leave all 24
single-point results unmoved. Its `--selftest` reproduces both open defects independently
before any sweep is read, and carries a facet-rotation invariant (`models/selfproof.scad` — a
tee, not a cylinder, because a cylinder maps onto itself under a facet rotation and would pass
vacuously).

It reads **exact ASCII STL** and reads the OFF alongside it, reporting agreement in an `agree`
column, so any exporter loss is visible rather than silent. `--selftest` runs 23 checks —
the known answers, the plumbing, a facet-rotation invariant, an exporter pair, and a
flaky/deterministic control pair — and refuses to sweep if any fails.

**The sweep has now been run: 353 cells against one pinned binary (`md5 21cd49a8`), 17 not
valid.** The live binary moved four times during the work; every row carries the binary it came
from. Five of the seventeen are the `$fn`=8 family, four are χ-odd with no other signal, and
both were invisible to the single-point bench. The instrument earned its cost on the run that
introduced it, which is what the bench itself did.

**Also stale:** all 24 models are now valid at stock defaults, so `fillet-bench/README.md`'s
"First run, 2026-08-04" table of five bad tiles no longer describes the tree — D22's closure
fixed them. `tee_small` reads `comp=2` at defaults, two closed surfaces rather than one solid,
which nothing has yet explained.

---

## 6. Branches and where records live

Only two worktrees exist on disk: the main checkout, and the render worktree. Everything from this
session is on `kerem-fillet`; nothing is stranded.

**Live:**

| branch | contents |
|---|---|
| `kerem-fillet` | **everything current.** 19 unpushed commits. |
| `worktree-agent-a1065db1b78cc5fd6` | render worktree. Its `fillet-feature-design/` content is a strict subset of `kerem-fillet`'s; source is merely older. Carries nothing unique — safe to delete. |

**One commit in history fails tests.** `0c0727583` implements an approach to `dropVolumelessParts`
that was then measured to break three unit cases and superseded by `4ce78926e`. It was not rebased
away because concurrent work had already committed on top of it. Squash it before the PR is
opened, or leave it and expect a bisect through that range to lie.

**Record archive** — read with `git show <branch>:<path>`, do not check out; these worktrees were
deleted:

| record | command |
|---|---|
| integration, 805 lines | `git show integ-three:work/INTEGRATION.md` |
| D20 | `git show worktree-agent-a1cc15393bdb9006e:work/NOTES.md` |
| D21 | `git show worktree-agent-ad442fb5e2bfb0c44:work/D21.md` |
| D17.3 | `git show fix-blockers:work/BLOCKERS-AB.md` |
| D19 | `git show worktree-agent-a60c66ea1a5277bb6:work/D19.md` |
| `Chain::verts` audit + patches | `git show audit-chain-verts:work/CHAIN-VERTS-AUDIT.md` |

Also present: `integ-all-four` (superseded integration attempt), and a number of
`worktree-agent-*` branches sitting at `944e0cbef`, `489946544` or `faf6e1762` that were never
advanced.

**This session's documents** are all on `kerem-fillet` and all now under `archive/`, except
`decisions/2026-08-04-arrives-straight-stays.md`, which stays at root: the measurements,
controls, reviews, and the `arrives-straight-images/` PNGs.

The whole of `fillet-feature-design/` is a dev artifact and is deleted before merge. Archiving
is about what the next reader has to open, not about preservation — git holds it either way.

---

## 8. PR review items still open

Lifted from `archive/pr-review.md` when it was archived. R3 and R4 are closed; R1, R2 and R5
were re-verified against the tree as still present on 2026-08-04.

**Blocking — all closed 2026-08-04.** R1, R2, R5, the experimental gating and the
documentation defect are done and committed; only R7's comment register and the "worth doing"
items below remain.

A trap worth keeping: commit `39557c022`, "Ship the fillet modules behind an experimental
flag, and drop them without Manifold", touched only `ACCEPTANCE.md` and `STATE.md`. It wrote
the criterion, not the code, and read as done for a day. **A commit subject in the imperative
is a claim; the diffstat is the evidence.**

**Stale entries, corrected 2026-08-05.** R1 and R5 were listed here as open and were already
committed — `563cd3423` and `539380344`. This is the mechanism behind the sense of circling:
the record over-reports open work, so each pass re-derives that the code is already fine
before discovering it. **Audit this section against the tree, not item by item.**

**R9 and R8 are done, 2026-08-05.** `077fca2fb` (degenerate triangle no longer classifies
convex — `classifyEdge` returned an edge endpoint as `aFar`, now returns −1, below every
threshold including `min_angle=0`), `a420eee16` (single-cell path drops volumeless parts),
`50358c776` (`endSections` read through `find()`, not `operator[]`), `95a2ee80c`
(`CORE_SOURCES` alphabetical), `4b2cfa41c` (warnings cut to one line each). Suite 2230/88
unfiltered, `ctest -R fillet` 21/21 both backends, binary verified with `strings` rather than
by mtime alone. R9b was harmless rather than wrong and was changed for consistency only.

**Left for R7:** the comment above `buildFilletTool` still says the diagnostic line goes out
"on every invocation". R1 made that false — a comment that now lies about behaviour, so fix
the clause rather than merely trimming it. Also note `fillet()` has no `debug=` parameter, so
after R1 the stats line is unreachable from `fillet()` at all; that is an unspecified
behaviour change.

- ~~**R1**~~ — **done, `563cd3423`.** `buildFilletTool` echoed a mesh-statistics line
  unconditionally, once per tool node and twice per `fillet()`. Now gated on `node.debug`;
  the conditional warnings below it stay.
- **R2** — on a build without Manifold, `fillet()` **deletes the model**.
  `GeometryEvaluator.cc:1065`, the `#else` branch, warns and leaves `geom` null. **Resolved
  differently from the review's proposal, on the owner's decision:** do not pass the child
  through — do not register the modules at all without Manifold, so calling one is an unknown-
  module error naming the line. `tests/CMakeLists.txt:1563` lists the cgal disables for the four
  `*-tool-tests` but not for `fillet-tests`, which needs either the three disables or a baseline
  holding in both configurations.
- **Experimental gating** — new, and not from the review. The five modules must register behind
  `Feature::ExperimentalFillet` the way `roof` does (`RoofNode.cc:62`); today all five in
  `register_builtin_fillet` (`FilletNode.cc:205`) pass no feature pointer and are
  unconditionally available. Every regression test that invokes them then needs
  `--enable=fillet`. Written up under Availability in `ACCEPTANCE.md`.
- **R5** — `TEST_CASE("zzdebug rib", "[.]")` at `FilletBuilder_test.cc:1577` is a scratch test
  hidden behind a Catch2 tag, and it ships. Delete it.
- **R7** — the comment register. Decided, not open: between a third and two fifths of
  `FilletBuilder.cc` is prose in a voice the tree does not use, and it does not ship in that
  form. Keep, at a line or two each — the value of a non-obvious constant and why it is that
  value; the failure a construction exists to avoid, stated as fact; an invariant a caller must
  not break; a genuine surprise in the geometry or in Manifold. Cut rhetorical framing, the
  narrative of alternatives tried, restatements of the code, and second-person address. Target
  roughly a third of current volume, same pass over `FilletBuilder_internal.h`, `FilletNode.cc`
  and the two test files. Landed commit messages are history and are not rewritten.

**Worth doing:** R6 — `FilletBuilder.cc` is now **3777 lines**, up from the 2518 the review
complained about; `FilletBuilder_internal.h` already names the six separable pieces. R8 — the
warnings are essays; OpenSCAD warnings are one line, so keep the first sentence and the
coordinates. R9 — `classifyEdge`'s `aFar` seeding makes a degenerate triangle answer "convex";
`unionCells` skips `dropVolumelessParts` only on the single-cell path; `epsAt` reads
`endSections[j.vert]` through `map::operator[]` on a read path; `FilletNode.cc` is out of
alphabetical order in `CORE_SOURCES`.

**Documentation defect, found 2026-08-04:** `fillet-pr/doc-page/fillet.md:291` says the tools
"warn and emit nothing" under the CGAL backend. That is wrong. The fillet path never reads
`RenderSettings::backend3D`; it is gated only on the compile-time `ENABLE_MANIFOLD`, the cgal
test disables sit under `if(NOT ENABLE_MANIFOLD)`, and
`tests/regression/render-cgal/round-tool-tests-expected.png` is a passing 19 KB render of real
filleted geometry. `--backend=cgal` is supported and tested. The true limitation is a build
without Manifold — which is R2.

---

## 7. Corrections to the earlier record, and broken instruments

Recorded here because each was believed and each was false.

- `handoff-2026-08-03.md:274` — "all 13 controls byte-identical either way" is **false**; four move
  (`pocket`, `rhomb`, `slab`, `corner`). Likely carried from a D17.3-in-isolation control, never
  re-checked on the integrated tree.
- The list of **15** nondeterministic corpus models omits `box_L_fn96_r1` and `box_T_fn96_r1`. Since
  the first was counted as a real mover, the recorded "5 of 210" is one high. The raw "17 of 225" is
  correct and not understated — 13 of the 17 are vertex-order noise.
- The "13 controls" are **12 distinct models**: `boss.scad` and `ctrl1.scad` are byte-identical.
- There are **two** `ScopedSizeGateRule` users, not three, and one of them *is* the scale-invariance
  test — both were guard-strips, neither a deletion.
- **The 2026-08-05 claim that `export_off.cc`'s six-figure precision made part of every failing
  set an exporter artefact is false**, and was committed to this file before it was checked.
  The precision loss is real — two vertices 5.7e-7 mm apart print as one OFF line, proven from
  the file — and it fires on 10 of 353 cells, but it changes **no verdict**. The supposed false
  reds are all invalid on exact STL across seven decades of weld tolerance, reading valid only
  at 1e-15, where nothing welds at all. The real disagreement was welded against unwelded.
  Reading exact STL is still the right default; the justification given for it was wrong.
- `kSeamOverMax`'s replacement comment claimed a ceiling of "about twice" 0.10·r. The sweep supports a
  **floor** at 0.08 and no upper edge short of 2.0; 2.0 was the top of the swept range, not a measured
  failure boundary. Corrected in source.

**New 2026-08-05 — a copy in the shared scratchpad is not a pin.** An agent pinned the binary
by copying the app bundle to the session scratchpad; a sibling agent's rebuild overwrote the
copy mid-run, and the original build no longer exists on the machine. Pin into a *private*
subdirectory. It was caught, and both builds were cross-checked as agreeing exactly on the
model in question, so the results stood — but this is precisely how measurements here have gone
wrong before.

**New 2026-08-05 — `mesh.py` has no non-manifold-vertex check**, so a surface pinched at a
point reads clean on `nonman` and announces itself only through χ parity, which is a coin flip
on the pinch count. This is instrument #11 and the first found by asking what the *validity
criterion* omits rather than whether a metric's value looked right. Four corrections follow,
and none of them is the tolerance — position-welding at 1e-6 is the right reading, because a
solid destined for manufacture is defined by its point set and not by its index table:

1. Add a non-manifold-vertex check — the faces incident on a vertex must form a single
   edge-connected fan; two or more fans is a pinch. Report it as its own count.
2. Stop reporting `genus` when the surface is pinched, as is already done for `bnd`/`nonman`.
   `genus=1` on `tee` r=0.5 is a number with no referent.
3. Flag `comp > 1` on a model whose source is a single union.
4. Report handle throat size beside genus, so a 1.5 µm handle can be triaged apart from a
   0.4 mm loose sliver instead of weighing the same.

**Instruments found broken (nine, cumulative):** `-o /dev/null` makes OpenSCAD skip the render and
report zero calls; `timeout(1)` does not exist on this machine and made an export loop report 19/19
FAILED; Catch2 splits test names on commas, so an unescaped test exclusion excludes nothing and
silently reports the full total. The standing rule — run any new metric on a case whose answer is
already known — earned its place again three times this session.

**Environmental:** ten agent runs were lost to the 10-minute stall watchdog, host process exit and a
network failure. Only committed work survived, every time.

---

## 9. What to do next, in order — written 2026-08-05

Ordered so that nothing later invalidates anything earlier. The first item is small and is the
reason the rest can be trusted.

**1. Fix `mesh.py`'s criterion — DONE 2026-08-05**, `90e62da6e` and `799b9733b`. All four
changes: `nmvert` counts fans per welded vertex and prints the first pinch's coordinates;
`genus` is suppressed when pinched; `comp` is compared against a per-model declaration the
model carries in its own source (`// mesh.py-comp: N`) rather than anything hardcoded, so
`shallow_crease` declares 2 and everything else expects 1; and a nonzero genus carries a
`throat` proxy — the first weld tolerance at which the genus stops being what it was.
`mesh.py --selftest` runs 11 synthetic controls, `sweep.sh --selftest` 49 checks, all passing.

**Two throat definitions were tried and are wrong**, documented in-source so they are not
re-derived: the closest non-face-sharing vertex pair reads 0.0015 mm on `hole_plate`'s
*legitimate* hole, because it measures facet spacing; adding a six-hop separation test then
reads 0.84 mm on `cross`, because a handle narrower than a facet has its sides one hop apart.
Welding is the only test that scales with the handle rather than with the tessellation.

**Also fixed, and it mattered:** a greedy-`sed` bug in `sweep.sh`'s `get comp` would have made
every shed fragment record as `comp=1` and agree with itself. Worth checking any other reader
of that line.

**2. Re-sweep under the corrected criterion — DONE 2026-08-05**, `b57bb6371`. All 353 cells on
one pinned binary (md5 `fd3dec78`, `src/` diffed clean against `11b6b3b1`), 3 renders per cell,
`distinct`=1 everywhere. **The true failure list is 21 cells, against the recorded 16:** five
newly fail (`tee` r=0.5, `tee_small` at defaults / `$fn`=10 / r=1.5, `cross` r=0.9), four change
reason from χ-parity to a *located* pinch, twelve are unchanged (the `$fn`=8 family, `cross`
r=0.3, `refused_neighbour` ×4, `rib_into_boss` 14 and 32), and **none went the other way**.
Results in `results/sweep-fd3dec78.tsv` and
`results/sweep-compare-11b6b3b1-vs-fd3dec78.txt`. The row gained three columns appended at the
end (`nmvert throat wantcomp`), so no recorded field index moved.

**2a. A1's scope is settled — owner decision 2026-08-05.** A1 is measured over the whole
353-cell sweep, but a cell may be closed as a **documented limitation** when the remnant's
measured scale is orders below the geometry it sits on. The deciding number is measured, never
argued, and must be recorded in the row that closes the cell. Written up under "A1's scope" in
`ACCEPTANCE.md`. This is what makes the gate reachable; without it, "every bench model valid"
had grown to mean 353 cells with no exceptions.

It changes no measurement below. It changes which of the 21 failures are *work*: `cross`'s
genus at r=1.5/2.0 is the worked example of a release note (1.5 µm throat), and the
pinched-vertex family is work.

**Sequencing, owner decision 2026-08-05:** do item 3, re-sweep, and scope items 4 and 5 against
what actually survives — not against what this file predicts will survive. Three of the §5 open
defects are plausibly one bug, so measuring after the fix is worth more than planning before it.

**3. Build the ball-seating check.** See §4d. Two parts, and the second may be the larger:
   - **Compare the seat foot against `r` — BUILT AND REVERTED 2026-08-05**, `06a12765a`,
     reverted at `fef81a712`, measured in §4d's last subsection. It confirms the 46° diagnosis
     on both `handblend_step` axes at the default threshold and it refuses genuine blends on
     every curved wall in the bench, a stock `tee` included. The prerequisite it exposed: the
     ball is seated from the crease's tangent planes, so on a wall that curves along the radius
     the tangency point is offset by, the constructed foot floats off the mesh by about r²/2R
     — 0.014·R on the tee, up to 0.43·r on a dome — and no local fit test can be believed until
     the ball is re-seated against the mesh. The 9.8e-15·R against 0.117–1.004·R separation is
     real for cap rims and flat-walled creases and does not survive adding curved walls to the
     population.
   - **Seat the ball against the mesh — BUILT 2026-08-05**, `2cd4f1431` and `5ec8d8fe1`,
     measured in §4d's last subsection. Newton on the two distances-to-mesh in the crease's
     section plane, plus an angular residual against the normal cone at the contact. It removes
     every curved-wall false refusal the previous part left — `tee`, `dome` and `boss_plate`
     clean at their baseline vertex counts — and on the one-parameter control it prints the
     derived `R(1−tan(Δ/2)) − d` exactly. At the default 46° it is still inert on that control,
     and now for a reason nothing local can reach: the ball genuinely seats on the merged
     wall-and-blend surface, to 8.9e-16. **The rule is finished; the grouping is what is left.**
   - **Split the threshold in two.** Promoted from an option to the next step by the line above.
     `FilletBuilder.cc:1068` hands `smoothSurfaces` the same number edge selection uses. One
     value must stay 46 — that is promise 2, settled. The other has to answer only "is this 7.5°
     turn a tessellation seam or a chamfer boundary", and Route 2's refutation says an angle
     alone cannot. Nothing else now blocks the size gate.
   - **Make the gate actually ask.** 15 of 26 accepted chains at r=0.5 have `ntest`=0 — every
     sample exempted as a chain end or as within `2·size` of a junction. A check that is never
     evaluated cannot refuse. This is the harder half: the exemptions exist for a reason and
     `FilletBuilder.cc:1156` explains it, so removing them naively will cause false refusals.

   Rejected on measurement, do not revisit: a relief threshold or minimum-arc rule leaves only
   a 1.46× gap between artifacts and genuine features, and a ball-buried test alone catches
   14/72. Both are recorded in §4d with numbers.

**3a. Regenerate `fillet-bench/sheets/` — the A5 instrument. Queued behind item 3.** The four
sheets and `INDEX.md` date from 2026-08-04 19:39 and are stale two ways, the second worse than
the first:

1. The renders predate `efe8e485a` (the out-of-bounds read) and `4ce78926e` (the
   `dropVolumelessParts` cap), so every image is from a binary that could build geometry out of
   allocator residue.
2. **The mesh column was computed by the pre-fix `mesh.py`, so it green-lights cells the
   corrected criterion fails.** `S1-T05` and `S1-T06` (`tee_small`, defaults and `$fn`=10) read
   `VALID … comp=2 chi=4 genus=0`, and item 1 found that model carries a fully detached
   6-triangle fragment, 0.35 × 0.10 × 0.40 mm, sharing zero vertices. The sheet asserts valid
   where the criterion now says not valid. A stale render is old; a stale *verdict* is a false
   acceptance sitting in the file the gate asks a person to review.

`sheet.sh:86` shells out to `mesh.py`, so regenerating picks up the corrected criterion —
`nmvert`, the suppressed genus, and the per-model `comp` declaration — with no change to
`sheet.sh`. Tile ids stay stable as long as `expect.txt` keeps its order.

Do it **after** item 3 lands, on the binary that will ship, for two reasons: A5 is reviewed by a
person and that review should happen once, and `sheet.sh` reads
`../build/OpenSCAD.app`, which is exactly what an implementing agent is rebuilding.

Known and not a defect: `sheet.sh` reads the OFF, where A1 reads exact ASCII STL, and it renders
each tile once where A1 needs three. Both are correct for A5 — `ACCEPTANCE.md` says outright
that the single-point contact sheet is not the form A1 holds in. The sheets are the junction
*eyeball*; `sweep.sh` is the measurement.

Also stale and fixable without a render: `fillet-bench/README.md:155`, "First run, 2026-08-04",
whose table of five bad tiles no longer describes the tree — D22's closure fixed them.

**4. `CleanupTopology()` without `CollapseShortEdges`** (§4d). Specified, not started. Needs a
vendored patch widening Manifold's public surface, or a pinch split against `MeshGL64`.
Rebase `worktree-agent-a2867183791ec2485` first — it is based on `efe8e485a` and `kerem-fillet`
has advanced past it, including in `FilletBuilder.cc`.

**5. R7, the comment register.** Mechanical, decided, and unchanged by any of the above. Do it
after the code stops moving, and fix the `buildFilletTool` comment that R1 made false.

**6. Squash `0c0727583`; delete `fillet-feature-design/` and the four geometry-blind
`fillet-tests` models.** Last, and only once everything above has landed — this file is the
working record until then.

**Note for whoever picks this up:** §4d says the same thing four independent investigations
said from four different starting points. That convergence is the strongest evidence in this
document, and it is worth more than any single measurement in it. The one-line summary is that
**the debris is pinched vertices, planted by the convex pass building beads at scales where no
bead belongs.** Three separate open defects on the §5 list are plausibly one bug.

**A correction to a path reference:** the builder is at `src/geometry/fillet/FilletBuilder.cc`.
Earlier entries in this file say `src/geometry/manifold/`, which is wrong.
