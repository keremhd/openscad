# Fillet operator — design and acceptance criteria

The gate the feature ships against. Everything not required here is a release note, not work.

## Scope

`fillet()`, plus the tool modules `fillet_tool` / `round_tool` / `chamfer_tool` / `bevel_tool`.
One size per invocation, optional selection brush, Manifold backend.

`fillet-pr/doc-page/fillet.md` is the authoritative description of what the operator does.
`fillet-operator-plan.md` is what it was meant to do; the two have drifted since 2026-07-29.

## Availability

**The five modules ship behind an experimental feature. Implemented 2026-08-04.**
`Feature::ExperimentalFillet("fillet", ...)` at `src/Feature.h:25` and `src/Feature.cc:52`,
passed as the second argument to each `Builtins::init` in `register_builtin_fillet`
(`src/core/FilletNode.cc:208-231`), exactly as `roof` does at `src/core/RoofNode.cc:62`. Users
opt in with `--enable=fillet` or the GUI preference.

This is the right shape for a first release of a large new geometry operator with known
documented limitations, and it makes those limitations a stated condition of use rather than a
surprise.

**And they are not registered at all on a build without Manifold. Implemented and verified by
building one.** `register_builtin_fillet`'s whole body sits inside `#ifdef ENABLE_MANIFOLD`
with an empty `#else`. On a real `-DENABLE_MANIFOLD=OFF -DENABLE_CGAL=ON` build, `round_tool`
and `fillet` are both `WARNING: Ignoring unknown module ... line 1`, with and without
`--enable=fillet`, and `ctest -N -R "fillet|tool-tests"` enumerates zero tests. That is
cleaner than the passthrough R2 proposed — an unregistered module names the line, where the
old code warned and silently returned nothing, which for `fillet()` deletes the model.

Consequences, all of which are part of this criterion and all now done:
- The five fillet `.scad` files are out of the `FEATURES_3D_FILES` glob and into a dedicated
  `--enable=fillet` block (`tests/CMakeLists.txt:1252-1284`), roof-style; ctest enumerates the
  identical 45 tests and all pass.
- **`tests/CMakeLists.txt:1563` needed neither disables nor a new baseline** — the whole block
  is inside `if(ENABLE_MANIFOLD)`, so without Manifold no fillet test is created at all. The
  old twelve-name disable list is gone.
- `CMakeLists.txt:1588` needed the same guard on the fillet unit-test sources, or
  `-DENABLE_MANIFOLD=OFF -DENABLE_TESTS=ON` does not compile.
- `fillet-bench/sheet.sh:18` now passes `--enable=fillet`.
- The unit tests call the builder directly and are unaffected.
- The user documentation states the flag in its first paragraph, not in a footnote.

## Promises

1. **Validity.** Any input yields a closed orientable manifold, or the crease is left
   unfilleted and a warning names it. A false refusal is the safe error; a false acceptance
   is the defect.

2. **Classification is a property of the mesh.** Which edges are features and which are
   curve-tessellation seams is decided from the solid alone. `$fn`, `$fa` and `$fs` affect
   only the tessellation of the blend surfaces the operator *builds*. They are not inputs to
   the classifier.

   A solid does not carry the settings that made it. It may be a union of primitives built at
   different `$fn`, an imported STL with no `$fn` at all, or a mesh that has been through a
   `resize()`. A classifier reading global render variables is answering a question about the
   file, not about the shape in front of it.

   **Settled 2026-08-04: the threshold is a constant, 46°.** Not derived from render
   settings, not derived from the mesh, not local. `min_angle` keeps its meaning and simply
   defaults to that constant. A constant reads nothing, so classification cannot depend on
   how the solid arrived and A2 passes by construction rather than by measurement.

   Where a mesh is genuinely ambiguous, or carries a real crease shallower than 46°,
   `min_angle` is the answer and the user is expected to reach for it — it is an explicit
   statement about their model, not an inferred render setting.

   Why 46 and not some other number, both bounds measured rather than chosen:
   45.0 is `cylinder($fn=8)`'s facet angle, and its wall seams must stay seams, so the
   constant must sit above it — and *not on it*, because a threshold equal to a common facet
   angle puts every octagon at the mercy of where `acos` lands within a few ulp. 46.2639 is
   the second quartet of the tee's concave intersection curve (16 edges in four symmetric
   quartets: 15.8583, 46.2639, 72.0210, 87.8188); above that line the ring fragments into
   seven chains and the size gate refuses four of them. 46.0 is the only round number in
   the window, and its 1.0° clearance from 45 exceeds the measured facet drift of a sphere's
   latitude rings, 0.065°, by fifteenfold.

3. **Named refusal.** A size that does not fit is dropped and warned, never silently clamped.
   Other creases on the same model are unaffected.

4. **Seams are acceptable.** A visible line where two blends meet is not a defect. The bar is
   a valid solid, not a smooth corner.

5. **Every tool accepts any solid, including one it produced.** A tool's output is a solid
   like any other, and a tool handed its own output — or another tool's — must behave as it
   does on a modelled solid. `fillet()` is exactly this composition, so the property is not
   optional.

   **Tagging first-pass geometry is ruled out. Owner decision 2026-08-05, not open.** The
   proposal was for the concave pass to mark the edges it created so the convex pass could
   skip them. It is rejected on principle: a tool that needs privileged knowledge of how its
   input was built is not reading a solid, and the same tool invoked by a user on an imported
   STL would have no tag to read. This is promise 2's argument applied to geometry rather
   than to the classifier — a solid does not carry the history that made it. Any fix must
   work from the mesh in front of it.

   **A feature far smaller than the radius is not a feature. Owner decision 2026-08-05.**
   Classification today reads dihedral angle and nothing else, which is scale-blind: a 90°
   step two microns high is indistinguishable from a 90° corner ten millimetres high. A tool
   asked for radius R cannot meaningfully blend a feature whose relief is orders of magnitude
   below R, and must skip it rather than build debris at the feature's own scale.

   This is not a violation of promise 2. What promise 2 forbids is reading `$fn`, `$fa` and
   `$fs` — render settings that describe the file rather than the shape. R is an explicit
   argument the user makes about the geometry, and it is the scale the operator was asked to
   work at. It is also the answer to the objection that refuted the local-threshold route
   below: that attempt failed because only magnitude separates 45° from 90° and no global
   scale was available. Here one is, and the user supplied it.

   It follows that a tool must tolerate anomalies in its input rather than depend on its
   input being clean — which is what promise 5 requires, and what an imported STL guarantees
   it will meet.

   The consequence, which is where the work goes instead: if a blend is tangent to the
   surfaces it meets, the edges it leaves behind are shallow and no threshold selects them.
   Where a second pass does select a first pass's output, **the question is why that edge is
   steep**, not how to hide it. A 46°–91° convex ridge on a rolling-ball blend's own output
   is a defect in the blend, and the leading suspect is a bead left blunt by a refusal or a
   truncation — a blunt end is a real 90° edge, and a classifier is right to see it.

## Documented limitations — these ship

- One size per invocation. No variable radius; no differing radii meeting at a corner.
- No runout: a blend that stops partway along an edge stops square.
- **Experimental: the modules require `--enable=fillet`.** See Availability above.
- **Requires a build with Manifold enabled**, where they are not registered at all. The runtime
  `--backend=cgal` *is* supported and tested: the node builds its tool through Manifold
  internally and hands the result to the CGAL pipeline, which is what
  `tests/regression/render-cgal/round-tool-tests-expected.png` is.
  `doc-page/fillet.md` stated this wrongly as "under the CGAL backend they warn and emit
  nothing", conflating the runtime backend with the build option. **Corrected 2026-08-04,
  `167536b62`** — it now names the build without Manifold as the limitation and says outright
  that `--backend=cgal` is supported. This entry stayed on the open list for two days after the
  fix landed and sent one agent to redo it; it is the §8 over-reporting fault in miniature.
- Re-filleting an already-blended model is not reliable.
- **The size gate refuses conservatively.** Some creases that could geometrically be blended
  are dropped with a warning (D23). Promise 1 makes this the correct failure direction.
- **And against a tangent blend it accepts conservatively, which is the wrong direction.**
  Owner decision 2026-08-05, after three measured attempts to fix it; the record is STATE.md
  §4d and §9 item 3. `FilletBuilder.cc:1068` passes one threshold to both edge selection and
  `smoothSurfaces`, so at 46° a tangent blend never reaches the crease threshold and merges
  into the wall it sits on. The seat test is then asked about the merged surface, whose
  curvature radius is R, and a radius-R ball genuinely seats anywhere on it. **The check
  cannot fire.** Measured on hand-built geometry with no fillet module in it: the default
  threshold accepts every clearance from 0.02 to 4.0, including one leaving 91% of the wall
  missing.

  A correct rule was built and measured — it reproduces the derived limit
  `R(1−tan(Δ/2)) − d` to six figures with no false refusals and no geometry loss — but it
  needs surface grouping finer than 45°, and no grouping value exists: the proof model needs
  below 7.5°, `cross` needs above 12° for its cylinder seam, and below 30° three flat-walled
  models false-refuse and shed half their vertices. **This is Route 2's refutation relocated
  to the second parameter**, not a new problem: a cube, a `$fn`=4 prism and a `$fn`=8 cylinder
  are locally congruent, and splitting the constant in two does not repeal that.

  This is why "re-filleting an already-blended model is not reliable" is a limitation and not
  a bug to be fixed before release. It is the same statement from the other end.

  One prerequisite is unbuilt and is the only place a successor should start: a contact landing
  on its own wall's boundary reads as a large miss, worth 11 unit-suite assertions, and it is
  the plausible cause of the flat-model false refusals that close the grouping window.
- **A crease shallower than 46° is not filleted unless `min_angle` says so.** This is the
  price of a constant threshold and it is paid on real models: the tee's own intersection
  curve carries a 15.86° quartet that is never selected by default. The operator picks one
  answer and the user overrides it. This is the documented purpose of `min_angle`, not a
  workaround, and the user documentation should say so where it currently presents the
  argument as a niche adjustment.
- **A tessellation coarser than `$fn`=8 has its wall seams filleted.** At 46° the cut falls
  between `$fn`=7 (51.43°, walls are features) and `$fn`=8 (45°, walls stay seams). Any
  constant cuts this sequence somewhere; what matters is that it does not cut *on* a common
  value. Stated so the boundary is documented rather than discovered.
- **`fillet()`'s second pass is not render-invariant, by construction.** It classifies the
  blended solid, whose blend arcs are genuinely tessellated to the render's `$fn`, so it is
  handed a different mesh at a different `$fn`. The classifier is invariant; the mesh it is
  given is not. Single-tool models match exactly, which is the control that proves the
  distinction. A release note, not a defect.

## The gate

Five checks. All absolute — none is a comparison against a previous build.

| | Check | Closes |
|---|---|---|
| A1 | Every bench model: zero edges carried by >2 faces, even Euler characteristic, genus as declared per model. **Every cell measured at least three times and aggregated to the worst outcome** — one render per cell is not sound; see below | the curved-arrival fin |
| A2 | **Provenance invariance** — see below | D22 |
| A3 | Every refusal warns and names its crease. **Second clause retired** — see below | D24 |
| A4 | Unit suite green on the pin: **2230 assertions / 88 cases — measured green 2026-08-05, six runs under six Catch2 seeds, on `cab639ffd`.** The three fillet failures on record are real below `4ce78926e` and absent at and above it; **no known pre-existing failures on this branch** | regression |
| A5 | Junction contact sheet, one render per bench model, reviewed by a person | the blind spot |

### A1 — why one render per cell does not measure it

`rib_into_boss` at `$fn`=14 returned a valid mesh on 2 of 40 identical invocations of one
unchanging binary, and an invalid one on the other 38 — a different topology, `f=448` against
`f=450`, not vertex-order noise. At `R`=1.0 the same model faults with SIGBUS on about one run
in six.

So a single render answers A1 correctly about 29 times in 30 and reports a real fault as clean
the other time. **A1 is only meaningful as a repeated measurement**: at least three runs per
cell, aggregated to the worst outcome, with the number of distinct meshes recorded.
`fillet-bench/sweep.sh --repeat` is the form that holds; the single-point contact sheet is not.

**A1 must state its weld tolerance, and must not read at one that welds nothing.** Manifold's
output is 2-manifold by index construction, so an unwelded reader cannot see a self-touch and
reads clean on a correct solid and a broken one alike — the same parity blindness that retired
`bnd`. Every disputed failure on record is invalid across weld 1e-4…1e-12 and valid only at
1e-15.

A1 should read exact ASCII STL rather than OFF, which prints six significant figures. This is a
precaution and not a correction: measured across 353 cells the OFF loses a vertex on ten of
them and changes the verdict on none. An earlier version of this file attributed several
failing sets to the exporter; that claim is withdrawn.

**The fault is found and fixed** — an out-of-bounds read in `chainBulges()` at a seam vertex,
`cab639ffd`. On binary md5 `11b6b3b1`: 60/60 one mesh at `$fn`=14, 40/40 at `R`=1.0 with no
SIGBUS, 20/20 at `$fn`=12 which now exports every run, and `distinct`=1 on all 353 sweep cells
across 1059 renders, against 14 cells at 2 before.

**The repeat stays anyway.** It is now the check that would notice the read coming back, and
`--selftest` asserts determinism on both control models rather than flakiness on one. A single
render is still not sound, because nothing proves the next such fault will announce itself.

**The A1 blocker is retired; the A1 failures are not.** Sixteen cells are not valid on the fixed
binary, all reproducing with identical mesh counts on both binaries. Of the seventeen recorded
before, exactly one — `rib_into_boss` at `$fn`=12 — was the memory fault. **A1 still cannot
pass, and now fails for reasons that are geometric.** Under the corrected criterion the true
figure is 21, not 16; see STATE.md §9 item 2.

### A1's scope — owner decision 2026-08-05

**A1 is measured over the whole 353-cell sweep, and a cell may be closed as a documented
limitation rather than fixed.** The bench grew from 24 single-point models to a `$fn` axis and
a radius axis, and reading A1 as "all 353 cells clean, no exceptions" makes the gate
unreachable — that reading is what the sweep would cost, not what the feature promises.

A cell qualifies for closure as a limitation only on a **measurement**, never on judgement: the
remnant's own scale, stated in millimetres, against the scale at which the solid is used. The
worked example is `cross`'s genus at r=1.5 and 2.0, whose handles measure a **1.5 µm** throat —
about 130× below a 0.2 mm layer, confirmed by the owner in a third-party slicer to slice as a
single object with no visible tunnel — and which sit in one octant of a solid with full
octahedral symmetry, which alone proves them boolean noise rather than intent.

This does not soften promise 1. A false acceptance is still the defect, and a fault at a scale
the user can reach is still work. What it settles is that a fault three orders of magnitude
below the geometry it sits on is a release note, and that the deciding number is measured
rather than argued. **Anything closed this way must carry its measured scale in the row that
closes it**, so a later reader can re-open it against a different manufacturing scale rather
than re-derive the whole question.

### A3 — why the "open bead" clause is gone

`bnd`, the count of edges carried by exactly one face, was the named instrument for it. **It is
identically zero on any Manifold-backend export, by parity**: the result is a Manifold boolean
so every edge carries exactly two faces, `mesh.py`'s weld only merges edges so every count
stays a sum of twos, and a face the weld collapses contributes two to the one edge it has
left. Confirmed both ways — every tile reads `bnd=0`, and the non-manifold edges that do
appear are carried by **four** faces, never three. `mesh.py` on a single-triangle OFF reads
`bnd=3`, so the script is sound; the measurement was blind on this backend.

So the clause was never passing — it was unmeasurable, and every green `bnd` column on record
carries no information. It is retired rather than re-instrumented because on a Manifold
backend an open bead cannot reach the output at all: the boolean closes it. What survives of
the original worry is a *blunt* bead end, which promise 4 already makes a release note, and
non-manifold edges, which `nonman` does measure.

Instrument #10, and the first one found by asking what a metric *could* say rather than
whether its value looked right.

### A2 — provenance invariance

The same solid must classify identically however it arrived. Two checks, on one crease set,
exact equality, on models of uniform tessellation:

1. **Defaults vs explicit.** A model with no tessellation variables, and the same model with
   `$fn` set to the fragment count it actually achieved, select the same creases. Neither
   needs `min_angle` — a plain tee at stock defaults is not an ambiguous mesh.
2. **Round trip.** The model exported to STL and re-imported selects the same creases as the
   original, with no `min_angle` on either side. No `$fn` exists on the import path at all,
   so this fails loudly on any classifier that consults a render variable.

This is a self-proving invariant of the kind the effort has repeatedly needed: it cannot pass
by accident.

**Status 2026-08-04: both checks pass, and a constant threshold makes them pass by
construction.** Check 1 measured identical first-pass classification across defaults and
explicit `$fn` on all 13 curved bench models; check 2's STL round trip gives 110 features /
31 concave / 79 convex either way. The A2 rows are kept as a regression guard, not because
they are still in doubt.

**Not a check:** a union of two cylinders built at different `$fn`. It belongs in the bench
as a *documentation* case — the model the manual uses to show why `min_angle` exists.
**But it no longer demonstrates that**: `mixed_fn` is valid at stock defaults without
`min_angle`, because a constant threshold does not care how many tessellation densities a
mesh carries. The exhibit needs replacing with one that fails for the reason the manual
claims — a real crease shallower than 46° is the natural candidate.

### Why absolute, not comparative

No clean-`944e0cbef` baseline exists anywhere in the current work; every measurement on record
is pre-change vs post-change. Acceptance that asks *is this solid valid* rather than *did this
change anything* makes that gap irrelevant, and retires byte-identity as an instrument — which
the integration record already concluded was the wrong test for a branch carrying two
default-on changes.

## What ships broken — the 21 not-valid cells, triaged 2026-08-06

Measured on binary md5 `fd3dec78` against `results/sweep-fd3dec78.tsv`, instrument and raw
output in `fillet-bench/work/s1-triage/`, four known answers reproduced before any new cell was
read. Weld 1e-6, scales in millimetres.

**The cut, applied uniformly:** a cell closes as a *documented limitation* only when its remnant
has a measured extent both **under 10 µm absolute** — an order below the finest manufacturing
resolution in common use — and **at least three orders below the solid it sits on**. That is
calibrated to the two worked precedents, `cross`'s 1.5 µm throat in a 40 mm solid and
`refused_neighbour`'s 0.34 µm sliver in a 33 mm one. A remnant with **no extent to measure** — a
zero-thickness membrane — cannot be closed on a measurement at all and ships as a defect.

**Result: 4 documented limitations, 17 known defects.** The expectation that most of the 21
would prove to be sub-manufacturing noise was wrong; `cross`'s handles were the exception, not
the pattern.

### Documented limitations — 4 cells, all `refused_neighbour`

A sliver wedge on 4 faces where one bead's spine crosses the neighbouring bead's tangency line,
at r = 0.2, 0.8, 0.9, 1.0. Extents 7.35e-5 to 4.05e-3 mm in a 32.9 mm solid — four to six
orders below the model. Smallest triangle 7.3e-9 mm². Below any manufacturing resolution and
below the tolerance of every slicer tested.

### Known defects — 17 cells, three families

These ship named. **All of them export a mesh that looks plausible**, which is the actual
hazard: the user cannot tell by inspection, and finds out when a slicer rejects the solid.

**1. Point-attached and detached slivers, 0.07–1.14 mm** — millimetre-scale, visible, and in
one case a separate printable object. Ten cells: `tee` r=0.5 (two slivers, 0.267 and 0.438 mm)
and r=0.9 (0.590 mm); `tee_oblique` r=0.2/0.3/0.8 (0.068–0.430 mm); `tee_small` at defaults, at
`$fn`=10 and at r=1.5 — a **fully detached** 6-triangle fragment sharing no vertex with the
solid, 0.353 × 0.101 × 0.401 mm and **growing with radius** to 1.139 mm; `cross` r=0.3 (0.080 mm
wedge) and r=0.9 (a detached shard outside the solid, winding number 0).

`tee_small`'s fragment growing with R is the clearest single statement that this is a
construction defect and not numerical noise.

**2. The `$fn`=8 boss-and-plate family, 5 cells** — `boss_plate`, `hole_plate`, `two_bosses`,
`dome`, `pipe_into_face`, every one valid at `$fn`≥10. The remnant is a **zero-area duplicate
triangle pair along the rim tangency line**, spanning 36–38 mm of a 57–66 mm solid, with a bead
overrun of 1.0e-3 to 2.0e-3. No thickness, so no scale to compare against a layer height.

**3. Zero-thickness membranes at a section plane, 2 cells** — `rib_into_boss` at `$fn`=14 and
32: a duplicate triangle pair with opposite orientation where consecutive cells abut, 0.44 mm
across. Present in the `fillet_tool()` solid alone, so `buildRoundSolid` produces it rather
than the caller's `union()`.

### Not in the 21, and still open on a different question

`cross` gains genus with radius — 0 at r≤1.0, 2 at r=1.5, 4 at r=2.0 — and every one of those
cells is *valid*, because a genus-4 closed solid is valid. Whether a fillet may punch handles
through the model is a promise question, not an A1 one. The handles measure a 1.5 µm throat and
slice as a single object, so they are a release note under any reading; they are listed here
only so their absence from the table above is not mistaken for an oversight.

## Triage rule

For anything found from here: **does it break promise 1 or promise 2?**
If no, it is a release note, not work.

## Current gate status

| item | verdict |
|---|---|
| curved-arrival fin | **closed 2026-08-04.** `arrivesStraight` deleted. The record was wrong about the fallback: there is no seated ball at those vertices — `chainJunctions` finds no junction, because the brush that leaves the crease unfilleted withholds the corner too, so the vertex fell to the stop-a-hair-short branch and the boolean resolved two beads meeting at no angle into a knife edge. `bcurve` `$fn`=64 goes χ=5/5 non-manifold → χ=2/0; across a 37-tessellation sweep, 16 invalid → 2 |
| D22 — classifier reads `$fa`/`$fn` | **closed 2026-08-04, by removal rather than repair.** The threshold is now the constant 46°, so no render variable and no mesh statistic is consulted. `tee`, `tee_oblique`, `tee_small` and `cross` are all valid at stock defaults |
| `cross` produces no mesh at all at stock defaults | **closed 2026-08-04.** Not an empty mesh: a 17.5 GB OOM SIGKILL before the exporter ran. Its threshold of 18.0° sat below the model's own 18.947° facet angle, so every facet seam became a crease, and the resulting component count reached the unguarded `Decompose()` at `FilletBuilder.cc:3400`. D22's tail, proven by a cliff at exactly 360/19 |
| D24 — bead truncated and left open at a refused neighbour | **closed 2026-08-04, does not reproduce.** The recorded repro renders valid with the feature confirmed firing. The visible symptom is a *blunt* bead end, not a hole — promise 4 makes that a release note. Mechanism understood, not merely absent: truncation applies only at vertices in `junctionAt`, and a refused crease is kept out twice over — `chainJunctions` runs over the post-gate chain set, and every surviving junction is dropped when `creaseLeavesUnfilleted` holds |
| A3 first clause — multi-refusal warning named only the worst crease | **closed 2026-08-04.** It named 1 of 4 on the repro. Now names every refused crease, capped at 24 then ", and N more". A real A3 failure that no instrument was watching |
| `refused_neighbour` non-manifold at r = 0.2, 0.8, 0.9, 1.0 | **open, new 2026-08-04. Breaks promise 1.** Valid at 0.3–0.7, 1.2, 1.5, 2.0. At r=0.9 a 0.34 µm sliver carried by 4 faces, stable across weld 1e-4…1e-9, on the concave bead's tangency boundary and nowhere near either refusal — the oblique junction, not the refusal. The bench carries r=0.5, which is valid, so a green bench hides it |
| latent A3 gap — `chainUsable[ci] = false` | **open, latent.** `buildRoundSolid` discards a two-station chain consumed by truncation with no warning. Its comment argues the node cannot reach it (shortest chain seen there: seventeen stations), so latent rather than live |
| D23 — size gate drops creases on impossible misses | documented limitation |
| D19 — subtractive scalloped ledge | parked, tag `d19-wall-recognition` |
| unguarded union of surviving parts, `dropVolumelessParts` | **closed 2026-08-05.** Bounded at 32 survivors. The recorded cause — an unguarded `Decompose()` on a high component count — is retired as wrong on measurement; the cost is the batch union of the survivors, and `Decompose` is innocent. `cross` completes at ≤387 MB where it was SIGKILLed at 17.5 GB. All 25 bench models byte-identical on exact STL, suite 2230/88 green |
| **the builder is nondeterministic in validity** | **closed 2026-08-05.** An out-of-bounds read in `chainBulges()`: a section overrunning a seam vertex takes a negative chain parameter, and `static_cast<int>(floor(q)) % nsta` stays negative, so the builder read the 24 bytes before a station buffer. Fixed in `cab639ffd`; 164 dedicated renders and a 1059-render sweep return one mesh per cell |
| **`rib_into_boss` SIGBUS at `R`=1.0** | **closed 2026-08-05.** Same read. 40/40 runs rc=0, one mesh, valid, χ=2 genus 0 |
| **χ-odd invalids with no non-manifold edge and no warning** | **open, new 2026-08-05. Breaks promise 1 silently.** `tee` at r=0.9 is χ=3, `nonman=0`, zero warnings; also `tee_oblique` at r=0.2/0.3/0.8 and `cross` at r=0.3. A false acceptance with no signal but parity — the defect direction the gate names as the unacceptable one |
| **`$fn`=8 invalidates five boss-and-plate models** | **open, new 2026-08-05.** `boss_plate`, `hole_plate`, `two_bosses`, `dome`, `pipe_into_face`, all valid at `$fn`≥10. A coarse-tessellation family the single-point bench could not see |
| `cross` gains genus with radius | **open, new 2026-08-05.** genus 0 at r≤1.0, 2 at r=1.5, 4 at r=2.0, all valid solids. A promise question — may a fillet punch handles through the model? — not an A1 one |
| **`rib_into_boss` SIGBUS at `R`=1.0** | **open, new 2026-08-05.** rc=138 and no output on roughly one run in six. A hard memory fault, and the likeliest cause of the nondeterminism above; under hunt with sanitizers |
| `rib_into_boss` invalid at `$fn` 14 and 32 | **open, and no longer flaky.** `$fn`=14 is invalid 60/60, `v=222 e=660 f=442`, nonman 3, χ=4, identical at weld 1e-4/1e-6/1e-9, **0 warnings**. `$fn`=32 is invalid with nonman 4 and χ=4 — worse than the pre-fix reading, which was reading garbage |
| ~~`rib_into_boss` invalid at `$fn`=14 and 32~~ (original) | **superseded 2026-08-05.** Same corner as the fin, smaller fault. Neither tessellation is one the bench renders — `expect.txt` has no `$fn` axis, so a fault appearing at some tessellations and not others is invisible to it |

## How D22 was closed, and the two routes that were tried and abandoned

Recorded because both were built and measured, and neither should be re-derived.

**Route 1, mesh-intrinsic threshold.** Take the dihedral angles of every two-face edge, drop
anything under 1°, cluster them, and read σ off the lowest dense band; threshold = 1.5σ. This
worked: it reproduced the known-good explicit-`$fn` threshold to two decimals on every plain
bench model, and took `tee`, `tee_oblique`, `tee_small` and `cross` from invalid to valid. It
died on a second-order problem. `fillet()` runs its round pass against the solid the fillet
pass left, and a blended solid honestly carries two tessellation scales — the model's facets
and the blend arcs at the call's `$fn`. When the arcs are finer they *are* the lowest dense
band, so σ collapsed to them: on `tee_large` at `$fn`=30 the threshold fell 18.0 → 9.3 and
the tile went to 231 non-manifold edges, χ=94. Taking the highest qualifying band instead
fixed that and kept every plain result, but rested on an absolute `< 90°` cap fitted against
one model.

**Route 2, a local threshold.** Compare each edge's turn against those of the facet strip it
sits in — scale-free, so mixed tessellation stops being a special case. **Refuted, and for a
structural reason worth keeping:** a cube, a `$fn`=4 prism and a `$fn`=8 cylinder are locally
congruent. Only magnitude separates 45° from 90°, so a scale must come from somewhere global.
Measured, a cylinder's wall seams and its rim edges both read a ratio of exactly 1.00 — the
same value for the case that must be a seam and the case that must be a feature, so no cut on
that measure exists. A variant anchored to a global scale leaked badly on plain models
(`hole_plate` 78 features against 56, `dome` 70 against 42) and did not fix the motivating
case.

**What shipped instead: a constant.** It satisfies promise 2 outright, makes A2 pass by
construction, dissolves the blended-mesh problem entirely, and makes `fillet()` pure sugar —
a user writing the same composition by hand gets the same number, with no C++-only privilege
threading a measurement between the two passes. The bounds on the value are measured; see
promise 2 above.

The lesson worth carrying: the two-scale problem was invisible until `fillet()`'s own output
was fed back through its own classifier. Any rule inferred from the mesh has to survive
reading a mesh the operator itself wrote.
