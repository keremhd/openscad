# START HERE — fillet / chamfer feature (read this first)

**Status: mid-rewrite, 2026-08-07.** If you are here to implement, read this page, then
`REQUIREMENTS.md`, then `spike/`. Do **not** implement from any other document in this folder —
the rest describe the *old* implementation being replaced, and will send you down abandoned routes
(this project has a documented history of exactly that).

## Implementation progress (2026-08-08)

The B1 rewrite is under way on branch `kerem-fillet` (`src/geometry/fillet/FilletBlend.cc`,
entry `buildBlend`). Built order §10: **step 1** (operators + registration), **step 2** (per-edge
topological bevel + local seam), **step 3** (single-sign corner ball), and the **planar half of
step 5** (mixed-sign ear-clip saddle: lbracket/box_step/rib/pocket) are done and committed.
Bench sweep — 18–20 models × {fn 0,16,32} × {r 0.5,1,2}, worst of 3 — is **162/162 built and
VALID, 0 fallback, 0 invalid**. Still open: **step 4** (partial selection — the brush spine-clip,
one-sided convex/concave, and the crowding/neighbour refusal), which needs per-vertex splitting
where a kept-sharp edge borders a blended surface; until it lands, partial selection returns the
model unchanged (loud, valid). The old swept-tool `FilletBuilder.cc` is compiled but unreferenced,
kept only for its retained mesh internals and their tests; delete it once step 4 subsumes brush +
refusal. See the `fillet-b1-implementation-state` memory for the working details.

## The one rule

**`REQUIREMENTS.md` is the authoritative spec. Implement from it and nothing else.** It supersedes
the API of every older doc here. Everything else is history or input that led to it.

## What the feature is now (one paragraph)

A **rewrite** (called "Path B"): two operators, `fillet(r, min_angle, convex, concave)` and
`chamfer(t, …)`, that **consume their children and return the blended solid**, editing the input
triangle mesh **directly** (topological bevel — no boolean kernel). This replaces the four
`*_tool` modules and the entire swept-tool machinery (stations/cells/seam-covers/canal/wedge/
beads). Per-edge concavity is handled automatically; mix-and-match is by nesting. See
`REQUIREMENTS.md` §1–§7a for the full contract.

## Documents — current vs historical

**CURRENT — read and implement from these:**
- **`REQUIREMENTS.md`** — the spec. Start here.
- **`along-sweep-stations.md`** — the second density floor (`$fn`-like cap on station spacing along
  the crease), which §9a names but never pinned down for B1. Fixes the straight-crease taper by
  splitting long selected feature edges before the blend. Default `k = 4` in `L = k·r`.
- **`spike/`** — the validated step-0 prototype (seam + poke-aware bridge) with its build report
  and the independent verification. Throwaway Python (uses `trimesh`), **not** production code, but
  it is the reference for the seam-reconciliation algorithm and its manifold/determinism checks.

**HISTORICAL — context only, do NOT implement from them:** (each carries a banner saying so)
- `ACCEPTANCE.md` — the *old* four-tool API. Its **promises** are inherited into `REQUIREMENTS.md`
  §5; its **API is superseded**.
- `STATE.md`, `TRAPS.md` — old swept-tool state and notes. Durable bits from `TRAPS.md`: ~6
  environment facts (locale, ASan, the build watchdog, ccache, BSD sed, python tooling) and 3
  measurement rules (state the weld tolerance; quote vertex counts beside verdicts; run a new
  instrument on a known answer). The rest is archaeology.
- `AUDIT.md` — an accurate description of the *old* artifact that is being replaced (332/353 cells,
  the 21 junction failures). Useful to understand what the rewrite deletes and why.
- `discussion.md` — the reframe conversation that produced `REQUIREMENTS.md`.
- `fillet-operator-plan.md`, `archive/**` (including the dated `log-*.md` files and
  `2026-08-06-redesign-unsolicited.md`) — historical plans, logs, and inputs.

## Where the gates stand (`REQUIREMENTS.md` §9)

- **Step 0 — the seam is tractable, local, deterministic, with a bounded poke-aware bridge:
  PROVEN.** 4 rounds of build → independent-verify → owner-check; see `spike/`. Its generality is
  analytical (the reconciliation reads only local inputs — contact polyline, incident facets,
  normals — so it is curvature- and source-invariant), not case-enumerated.
- **Step (a) — contact placement: GUARANTEED, not a gate (retired).** Placement is a per-tip 1-D
  search = "walk setback `t` along the wall from the crease," which always lands on the surface by
  construction. The old "0.117·R artifact-floor separation" kill test was a holdover from the
  swept-tool/seat-gate architecture and does not apply to B1 (no boolean, no seat gate). Off-face
  and non-monotone-predicate are *detected* boundary cases, not separation problems. No spike.
- **Step (b) — the mixed-sign vertex-star saddle patch: DEFERRED TO LAST (owner).** Net-new, the
  final step. Until then **mixed-sign vertices are left unpatched** — a documented gap (those
  corners stay crude/unclosed, matching or improving on today's valid-but-wrong behaviour). Judge
  it on the **visual junction sheet**, not A1 validity (`REQUIREMENTS.md` §8).

## Suggested build order (`REQUIREMENTS.md` §10)

1. `fillet()`/`chamfer()` operator skeleton + module registration (§1).
2. Per-edge contact placement (per-tip search) + seam reconciliation — **port the algorithm from
   `spike/`** (§7a option 3); run against `fillet-bench/`, quote vertex counts.
3. Single-sign junctions — the corner-ball idea (§3a), re-expressed in B1.
4. Selection (spine-clip + `convex`/`concave` + `min_angle`, §4) + the crowding/neighbour refusal
   (§5a c — the one open detection problem).
5. **Last: the mixed-sign saddle** (§9 b) — leave those vertices unpatched until then.

## Validation

`fillet-bench/` is the implementation-independent validity oracle (unchanged). Rules that are not
optional: quote **vertex counts** beside every verdict (against a plain unblended render); measure
each cell **≥3× and take the worst**; judge **mixed-sign junctions on the visual sheet**.
