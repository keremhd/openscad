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

   The automatic threshold must be right on a model of uniform tessellation. Where a mesh is
   genuinely ambiguous, `min_angle` is the answer and the user is expected to reach for it —
   it is an explicit statement about their model, not an inferred render setting.

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
- **An ambiguous mesh may classify wrongly, and `min_angle` is the answer.** A model mixing
  tessellation densities has no single automatic threshold that is right everywhere; neither
  does a flat land between two chamfers, which is locally congruent to one facet of a coarse
  cylinder (D16). The operator picks one answer and the user overrides it. This is the
  documented purpose of `min_angle`, not a workaround, and the user documentation should say
  so where it currently presents the argument as a niche adjustment.

## The gate

Five checks. All absolute — none is a comparison against a previous build.

| | Check | Closes |
|---|---|---|
| A1 | Every bench model: zero edges carried by >2 faces, even Euler characteristic, genus as declared per model | the curved-arrival fin |
| A2 | **Provenance invariance** — see below | D22 |
| A3 | Every refusal warns and names its crease, and no refusal leaves an open bead | D24 |
| A4 | Unit suite green on the pin: 1709 assertions / 85 cases | regression |
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

**Not a check:** a union of two cylinders built at different `$fn`. No single automatic
threshold is right for both, and that is what `min_angle` is for. It belongs in the bench as a
*documentation* case — the model the manual uses to show why the argument exists.

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
| curved-arrival fin — non-manifold, odd χ, inside the `arrivesStraight` fallback | **blocks** (A1) |
| D22 — classifier reads `$fa`/`$fn` | **blocks** (A2). Both recorded fix directions are disqualified: each keeps the classifier reading render settings. Smaller than it looks — `min_angle` carries the ambiguous cases |
| D24 — bead truncated and left open at a refused neighbour | **blocks** (A3) |
| D23 — size gate drops creases on impossible misses | documented limitation |
| D19 — subtractive scalloped ledge | parked, tag `d19-wall-recognition` |

## Note on the route for D22

Both directions recorded in `handoff-2026-08-03.md` keep the classifier reading render
settings — one of them by adding plumbing to read them more accurately. Neither satisfies
promise 2.

What is needed is narrower than it first appears. Because `min_angle` carries the ambiguous
cases by design, the automatic threshold does **not** have to separate D16's congruence, and no
non-local cylinder-fitting pass is required for it. It has to be derived from the mesh and be
right on a model of uniform tessellation — enough to pass A2's two checks.

That leaves the question to answer first: what mesh-intrinsic quantity the default threshold
comes from. It has to be available for an imported STL, so it can only be read off the geometry
— the distribution of dihedral angles across the solid, or each edge's angle against those of
the facet strip it sits in. Establish that before writing code; it is a smaller change than
either recorded direction, not a larger one.
