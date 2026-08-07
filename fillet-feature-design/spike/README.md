# Step-0 spike — validated seam + poke-aware bridge

**Throwaway prototype, not production code.** This is the reference that established
`REQUIREMENTS.md` step 0 (the direct-mesh-edit seam is tractable, local, deterministic, and the
poke-aware bridge is bounded). Produced over 4 rounds of build → independent-verify → owner-check,
2026-08-07. Python + `trimesh`.

## What it proves (see `SPIKE-REPORT.md` and `VERIFY-REPORT.md`)

- Overshoot + **local clip against the actual (jittered) facets** yields a manifold, deterministic,
  local seam — no analytic shortcuts; contact via real `trimesh.section`/`ray`.
- Poke-out failures are **genuine and localized** to the poking spans (convex control stays closed).
- The **sub-interval bridge is bounded**: added DOF stay flat when the wall is refined *away* from
  the concavity and grow only with the concavity's own tessellation. Every build watertight.
- Determinism is byte-identical — and it caught a real bug: `trimesh.signed_distance` casts random
  rays; the poke test uses nearest-point + face-normal sign instead.

## Files

- `spike3.py` — the current prototype (sub-interval bridge, `poking_nodes`/`plan_seam`, two-axis
  locality test). `spike2.py` — the mesh rig it reuses. `meshutil.py` — the manifold / determinism
  checker (independently verified sound; reuse its checks).
- `verify_indep.py`, `verify2.py` — the independent verifier's own checks.
- `SPIKE-REPORT.md` — build report. `VERIFY-REPORT.md` — independent verification (incl. the
  boundedness sweep extended to 1025 nodes). `spike3_output.txt` — raw run.

## For the implementer

The **algorithm** ports to production; the **code** does not (Python/trimesh throwaway). Note two
things carried into `REQUIREMENTS.md`: the seam stitch *triangle* count is O(fine nodes under the
span) — inherent to coarse-to-fine stitching, acceptable; and this prototype extracted the contact
polyline by a planar `z=t` section, which is **extrusion-specific** — on doubly-curved walls the
contact polyline comes from a per-tip search (that is step (a), not the reconciliation).
