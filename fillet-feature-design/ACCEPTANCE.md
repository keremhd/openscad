# Fillet operator — design and acceptance criteria

The gate the feature ships against. Everything not required here is a release note, not work.

## Scope

`fillet()`, plus the tool modules `fillet_tool` / `round_tool` / `chamfer_tool` / `bevel_tool`.
One size per invocation, optional selection brush, Manifold backend.

`fillet-pr/doc-page/fillet.md` is the authoritative description of what the operator does.
`fillet-operator-plan.md` is what it was meant to do; the two have drifted since 2026-07-29.

## Availability

**The five modules ship behind an experimental feature.** A new
`Feature::ExperimentalFillet("fillet", ...)` in `src/Feature.h` and `src/Feature.cc`, passed as
the second argument to each `Builtins::init` in `register_builtin_fillet`
(`src/core/FilletNode.cc:205`), exactly as `roof` does at `src/core/RoofNode.cc:62`. Today all
five register unconditionally. Users opt in with `--enable=fillet` or the GUI preference.

This is the right shape for a first release of a large new geometry operator with known
documented limitations, and it makes those limitations a stated condition of use rather than a
surprise.

**And they are not registered at all on a build without Manifold.** Decided: the feature is
simply unavailable there rather than degrading. That is cleaner than the passthrough R2
proposed — an unregistered module is an "unknown module" error naming the line, where the
current code warns and silently returns nothing, which for `fillet()` deletes the model.

Consequences, all of which are part of this criterion:
- Every regression test invoking the modules passes `--enable=fillet`, following the
  `--enable=predictible-output` and `--enable=lazy-union` precedent in `tests/CMakeLists.txt`.
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

## Documented limitations — these ship

- One size per invocation. No variable radius; no differing radii meeting at a corner.
- No runout: a blend that stops partway along an edge stops square.
- **Experimental: the modules require `--enable=fillet`.** See Availability above.
- **Requires a build with Manifold enabled**, where they are not registered at all. The runtime
  `--backend=cgal` *is* supported and tested: the node builds its tool through Manifold
  internally and hands the result to the CGAL pipeline, which is what
  `tests/regression/render-cgal/round-tool-tests-expected.png` is.
  `doc-page/fillet.md:291` states this wrongly as "under the CGAL backend they warn and emit
  nothing" — it conflates the runtime backend with the build option, and must be corrected.
- Re-filleting an already-blended model is not reliable.
- **The size gate refuses conservatively.** Some creases that could geometrically be blended
  are dropped with a warning (D23). Promise 1 makes this the correct failure direction.
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
| A1 | Every bench model: zero edges carried by >2 faces, even Euler characteristic, genus as declared per model | the curved-arrival fin |
| A2 | **Provenance invariance** — see below | D22 |
| A3 | Every refusal warns and names its crease, and no refusal leaves an open bead | D24 |
| A4 | Unit suite green on the pin: **2230 assertions / 88 cases** | regression |
| A5 | Junction contact sheet, one render per bench model, reviewed by a person | the blind spot |

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

## Triage rule

For anything found from here: **does it break promise 1 or promise 2?**
If no, it is a release note, not work.

## Current gate status

| item | verdict |
|---|---|
| curved-arrival fin | **closed 2026-08-04.** `arrivesStraight` deleted. The record was wrong about the fallback: there is no seated ball at those vertices — `chainJunctions` finds no junction, because the brush that leaves the crease unfilleted withholds the corner too, so the vertex fell to the stop-a-hair-short branch and the boolean resolved two beads meeting at no angle into a knife edge. `bcurve` `$fn`=64 goes χ=5/5 non-manifold → χ=2/0; across a 37-tessellation sweep, 16 invalid → 2 |
| D22 — classifier reads `$fa`/`$fn` | **closed 2026-08-04, by removal rather than repair.** The threshold is now the constant 46°, so no render variable and no mesh statistic is consulted. `tee`, `tee_oblique`, `tee_small` and `cross` are all valid at stock defaults |
| `cross` produces no mesh at all at stock defaults | **closed 2026-08-04.** Not an empty mesh: a 17.5 GB OOM SIGKILL before the exporter ran. Its threshold of 18.0° sat below the model's own 18.947° facet angle, so every facet seam became a crease, and the resulting component count reached the unguarded `Decompose()` at `FilletBuilder.cc:3400`. D22's tail, proven by a cliff at exactly 360/19 |
| D24 — bead truncated and left open at a refused neighbour | **open, blocks A3** — but the bench reads `bnd=0` on all 35 tiles, including tiles where four chains are refused, so it may be masked rather than live. Under investigation |
| D23 — size gate drops creases on impossible misses | documented limitation |
| D19 — subtractive scalloped ledge | parked, tag `d19-wall-recognition` |
| unguarded `Decompose()` on the finished solid, `FilletBuilder.cc:3400` | **open, new 2026-08-04.** Any high component count reaches it and it materialises a full mesh per component — 17.5 GB and SIGKILL on the `cross` repro. The `unionCells` comment at `:1421` already names this hazard. Fixing the threshold stopped `cross` reaching it; it did not make the path safe |
| `rib_into_boss` invalid at `$fn`=14 and 32 | **open, new 2026-08-04.** Same corner as the fin, smaller fault. Neither tessellation is one the bench renders — `expect.txt` has no `$fn` axis, so a fault appearing at some tessellations and not others is invisible to it |

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
