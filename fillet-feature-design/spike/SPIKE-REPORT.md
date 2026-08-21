# B1 topological-bevel — SPIKE step 0 report (Round 4: bounded local bridge)

**Question:** is a *local* seam between a built blend patch and a *tessellated
curved wall* tractable, deterministic, local, **and bounded** — with a bridge
fallback that always yields a closed solid?

Progress across rounds (all independently verified):

- **R2** got the real facet-walk clip, manifold seam, locality, determinism —
  **confirmed**.
- **R3** got the consistent fine/coarse stitch, genuine poke-driven failure, and
  a deterministic poke test — **confirmed**. But its "local bridge" was
  **refuted**: a locality stress test (wall NY 41→321, dent fixed) showed the
  one-span bridge pulled in *every* fine node under the span
  (`local_verts == full_res_verts` at each level). Cost scaled with wall
  tessellation, not with the poking region. Valid, but **not bounded**.
- **R4 (this report)** replaces the whole-span bridge with a **sub-interval
  bridge** and proves boundedness with a two-axis test.

**Verdict: BOUNDED LOCAL BRIDGE PROVEN.** The bridge inserts fine seam nodes only
across the concave sub-interval; its cost is **flat** against unrelated wall
density and grows only with the concave feature's own tessellation (necessary
fidelity). All R2/R3 properties are preserved.

Files: `spike3.py` (R4 construction + driver), `spike2.py` (`Wall` rig + real
trimesh queries, reused), `meshutil.py` (manifold + determinism checks,
**unchanged**), `spike3_output.txt` (raw run). Env: Python 3.14, `numpy 2.5.1`,
`trimesh 5.0.0`, `scipy`, `rtree`, `networkx`. `python spike3.py` reproduces all.

---

## The fix (what changed from R3)

R3 bridged a poking span by fine-resolving the **whole** span (a station at every
fine node from `a` to `b`). R4 (`plan_seam` + `poking_nodes`) inserts fine seam
stations **only at the nodes where the coarse chord is actually in air**
(`Wt[k] < chord_x − tol`, read from the real facet vertices), iterating until no
span pokes. The non-concave shoulders of the span stay coarse. Every span — coarse
shoulder or fine dent step — is then closed by the same fan-stitch, so the corridor
seam (Sw) vertex count equals the number of stations, which now tracks the dent,
not the wall.

**Metric correction (the verifier's key point):** the bridge-cost metric counts
**only corridor seam (Sw) vertices**, excluding the retained fine wall/plate
(`W1,PI,PO,…`). Those are full-res by design in every case and were masking the
R3 comparison (`total_verts` looked local only because the retained wall dominated
it either way).

## THE DECISIVE TWO-AXIS LOCALITY TEST

Clean rig (`make_wall`): the concave footprint is fixed at `dent_c ± 3·dent_w`;
nodes inside it = the dent's tessellation; background nodes are placed **only
outside** it. Coarse stations bracket the dent (positions 0, 2, 8, 10 → the single
coarse span 2–8 spans the whole dent, forcing the bridge to insert nodes). No
radial jitter, so the Gaussian dent is the only concavity. `bridge_added_verts` =
corridor Sw vertices beyond the coarse baseline.

### (a) Refine the wall AWAY from the dent (dent tessellation FIXED at 9)

| background nodes | total nodes | **bridge-added (sub-interval, R4)** | R3 whole-span would add |
|---:|---:|---:|---:|
| 9   | 15  | **3** | 9 |
| 17  | 19  | **3** | 11 |
| 33  | 29  | **3** | 15 |
| 65  | 47  | **3** | 19 |
| 129 | 85  | **3** | 31 |
| 257 | 159 | **3** | 55 |

**FLAT at 3** across a 28× increase in background density. The R3 whole-span
figure (right column) grows 9 → 55 on the same sweep — that is exactly the
unboundedness the verifier found, and it is gone. All builds watertight.

### (b) Refine the DENT itself (background FIXED at 33)

| dent nodes | total nodes | **bridge-added** |
|---:|---:|---:|
| 5  | 25 | 1  |
| 9  | 29 | 3  |
| 17 | 37 | 7  |
| 33 | 53 | 13 |
| 65 | 85 | 27 |

**GROWS ~linearly with the dent's own tessellation.** This is *necessary* fidelity
— you cannot hug a fine concave surface with fewer points — and is labelled
acceptable, not a failure.

**Verdict: (a) flat + (b) grows ⇒ the bridge cost is tied to the concave feature
it must follow, not to unrelated wall density. BOUNDED LOCAL BRIDGE PROVEN.**

---

## Carried-forward properties (re-run under R4, unchanged)

- **Consistent seam (no-poke ⇒ watertight at any station count):** convex no-poke
  wall watertight at 2/3/5/12 stations, χ=2, 0 boundary, 0 dup faces.
- **Genuine poke-driven failure:** dent wall, coarse, no bridge → open spans are
  exactly the 2 poking spans (`[0,8]`, `[16,24]`); non-poking spans watertight
  even coarse. Poke-caused, not tessellation mismatch.
- **subdiv_limit honesty:** no-bridge stays holed until subdivision reaches the
  fine nodes; bridge closes at any limit; poke-out is non-monotone under uniform
  subdivision (⇒ adaptive placement, matching the redesign note).
- **Determinism:** base mesh vertex+face order shuffled before every real query;
  3/3 shuffles byte-identical. (R3's `signed_distance` random-ray nondeterminism
  was replaced by nearest-point + face-normal sign; the sub-interval `poking_nodes`
  test is likewise deterministic.)
- **Mid-facet ray gap:** 0.103 mm mid-facet vs 1.8e-15 at nodes.
- **Normal side-filter (thin wall):** keeps 2 near faces, discards 2 far.
- **Doubly-curved wall (bonus):** on an icosphere cap, the section clip walks 82
  facets, ray contacts are deterministic under shuffle, and a doubly-curved
  concavity produces a 1.50 mm poke — the clip/contact/poke primitives port to
  double curvature. (Full bridged dome-seam mesh not assembled — scope limit.)

## Verdict per hard part

| property | status | evidence |
|---|:--:|---|
| consistent seam (no-poke watertight at any count) | PROVED | 2/3/5/12 stations, χ=2, 0 boundary |
| poke ⇒ hole (poke-caused) | PROVED | only the 2 poking spans open |
| local, poke-aware bridge | PROVED | inserts only concave-sub-interval nodes |
| **bridge is BOUNDED** | **PROVED** | axis (a) flat at 3 over 28× wall density; axis (b) grows with dent only |
| determinism | PROVED | 3/3 identical under shuffle |
| mid-facet ray gap | PROVED | 0.103 mm vs 1.8e-15 |
| doubly-curved primitives | PARTIAL | clip/contact/poke port + deterministic; full dome seam not built |

**Conclusion.** A tractable, deterministic, local, **and bounded** seam with a
poke-aware bridge is achievable. The bridge closes any real poke-out to a
watertight solid; its cost is independent of unrelated wall tessellation and
scales only with the concave feature it must follow. No blocker to the B1
local-seam foundation was found.

Honest scope limits: representative programmatic geometry (jittered/curved L-block
wall + Gaussian dent; icosphere for the bonus), not the actual bench STLs; the
fan-stitch is one concrete, sufficient realization of §7a option 3; the full
doubly-curved bridged seam mesh was not assembled; spike steps (a) search
separation and (b) the mixed-sign saddle remain out of scope for step 0.

## Reproduce

```
cd scratchpad
./venv/bin/python spike3.py          # prints every number above, incl. the two-axis table
# fresh env: python3 -m venv venv && ./venv/bin/pip install numpy trimesh scipy rtree networkx
```
