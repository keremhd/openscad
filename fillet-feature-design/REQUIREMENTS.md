# Blending feature — requirements

**Status: requirements draft, 2026-08-07. Plain geometric language, no internal method
presumed.** This supersedes the *API* of `ACCEPTANCE.md` (four `*_tool` modules) and the
architecture vocabulary of the archive. It **inherits, unchanged**, the promises and the
validity oracle those documents paid for — they are properties of *the feature*, not of the
swept-tool implementation, and survive the rewrite.

Written after the owner chose a full rewrite over tuning the 332/353 swept-tool artifact
(`AUDIT.md`), on the grounds that the two surviving defects are bead–bead junction artifacts of
that architecture and vanish with it. See [`discussion.md`](discussion.md) for the reframe this
rests on and [`AUDIT.md`](AUDIT.md) for what the discarded artifact actually was.

---

## 1. The API

Two operators, symmetric. Each **consumes its children and returns the finished blended
solid** — not a tool to be composed by the user.

```
fillet(r = 3, min_angle = 46, convex = true, concave = true) { model(); brushA(); brushB(); }
chamfer(t = 1, min_angle = 46, convex = true, concave = true) { model(); brushA(); }
```

- **First child is the model.** Remaining children are brushes (§4). No brush ⇒ **all feature
  edges** (the whole model is the region).
- **`fillet` subsumes today's `fillet_tool` + `round_tool`.** It applies an *arc* blend to
  every selected edge, **adding material on concave edges and removing it on convex edges**,
  and returns one solid. Radius `r`.
- **`chamfer` subsumes today's `chamfer_tool` + its concave sibling.** *Flat* cut, both signs,
  one solid. Setback `t`.
- **Concavity is not the user's job.** The user puts concave edges, convex edges, or both into
  the brushes; the operator reads each edge's sign from the mesh and does the right thing.
- **`min_angle` defaults to 46.** Its only job is selection — which edges are creases the user
  wants blended vs. curve-tessellation seams (§4, promise 2). It is **not** read anywhere in
  the geometry construction.
- **`convex` / `concave` default true — a sign filter, orthogonal to the brush.** A brush selects
  by *location* (a volume); both convex and concave edges pass through the same volume, so the
  flags are the only way to say "only the ridges" or "only the valleys" over a region. `convex`
  keeps ridge edges (fillet rounds/removes), `concave` keeps valley edges (fillet fills/adds).
  **Both false is a usage error: warn and return the model unchanged** — never silently drop it
  (promise 1). Same two flags on `chamfer`, symmetric.

Why the operator form and not tools: it dissolves the negative-union problem outright. A
treatment that both adds and removes material cannot be *returned* as one signed value in a
language with no negative geometry — but an operator that returns the *finished* solid computes
`model + concave-blends − convex-blends` internally and hands back one ordinary solid. Nothing
signed ever escapes. (`discussion.md` §5a reached this; the current `fillet()` APPLY entry,
`GeometryEvaluator.cc:1044`, already runs both signs in one shot and is the proof it works.)

## 2. What each operator promises geometrically

- **`fillet(r)`** — every selected edge is replaced by a circular arc of radius `r` tangent to
  both incident faces. Concave ⇒ the arc adds a fillet; convex ⇒ the arc rounds the corner off.
- **`chamfer(t)`** — every selected edge is replaced by a flat cut set back `t` from the edge
  along each face.
- **`fillet` and `chamfer` are separate operations with separate parameters.** They are never
  combined except by nesting (§3), where each sees the prior result as an ordinary solid and is
  unaware of the other — so there is no runtime interaction between `r` and `t` to reconcile or
  warn about. The only obligation is documentation: `r` (fillet radius) and `t` (chamfer setback)
  are not interchangeable and must not be presented as the same knob. For reference, they measure
  different things — fillet band width `2r·cos(ψ/2)` widens as the edge sharpens, chamfer band
  width `2t·sin(ψ/2)` narrows — but a user never chooses between them on one edge in one call.

## 3. Composition is nesting, and it is ordered

Mix-and-match is achieved by **nesting**, not by the user gluing tools together:

```
chamfer(t = 1) { fillet(r = 3) { model(); brushA(); } brushB(); }
```

The inner blend's returned solid *is* the outer operator's model. Consequences, all required
behaviour:

- **Coherent by construction.** Each stage sees the fully-resolved prior result, so there is no
  "declaration drift" — the failure mode `discussion.md` §5b had to defend the tool form
  against does not arise here.
- **Ordered.** `chamfer∘fillet ≠ fillet∘chamfer` where the two regions are adjacent or
  overlapping. This is accepted, not a defect. (It matches how APPLY already runs concave
  before convex in a fixed order.) Disjoint regions commute.
- **Promise 5 (below) is this property.** A blended solid is an ordinary solid; an operator
  handed one must behave as it does on a modelled solid, including one it produced itself.

## 3a. Junctions — one class is solved, two are the invention

Junctions split by the **signs of the selected edges meeting at a vertex**, and the split is not
cosmetic: it is decided by whether a *single-signed* corner primitive can fit.

**Single-sign junctions are solved, and the idea is preserved.** Where every selected edge at a
vertex is the same sign — all convex, or all concave, *including* the case where a filleted edge
dies into an unfilleted one — a one-signed corner cap fits: intersect a sphere at a convex vertex
(rounds it off), union a sphere at a concave one (fills it). This is "put a ball/bead at the
corner," it worked in the shipped code, and it is the epitome of the §7 approximate-and-explainable
principle. **The idea carries to B1; the swept-tool `cornerCell`/`cornerProfile` code does not.**
Re-express it as a one-signed vertex-star cap — and do *not* rebuild it with the tangent-surface
exactness the recent effort circled on. Simplicity is the requirement, not accuracy.

**Mixed-sign junctions are unsolved and are the reason for the rewrite.** Where a convex and a
concave selected edge meet at one vertex, the corner region must *simultaneously* remove material
on the convex side and add it on the concave side. **No single sphere does both** — a ball is one
sign of curvature, so the correct patch is a **saddle**, for which there is no simple primitive.
This is exactly why calling the add-tool then the subtract-tool in sequence *provably cannot*
resolve it: each pass authors its own half and they meet along a seam neither computed, with no
geometry-only rule to recover the joint answer — hence the order-dependence
(`chamfer∘fillet ≠ fillet∘chamfer`) and the circling. Two sub-cases, both **net-new design**:

- **convex + concave** — the saddle vertex-star patch.
- **convex + concave + unfilleted** — the same saddle, additionally running out into a flat,
  untouched region. The hardest case, an escalation of the one above.

**Unifying mechanism:** the vertex-star patch is general; the single-signed corner ball is its
easy, already-working case; the invention is the two-signed (saddle) case. Requirements that
follow, binding on B1:

- **The atomic unit of construction is the vertex neighbourhood (the star of a selected vertex),
  not the edge**, wherever selected edges of *mixed sign* meet there. That star is authored as
  **one patch in one pass**, never "compute the convex part, compute the concave part, glue."
- Single-sign runs *may* be built per-edge and capped with the one-signed corner — that is why the
  single-sign case always worked and is the only reason per-edge construction is admissible.
- **Build-per-edge-and-glue across a sign change is forbidden**: the glue seam is exactly where
  the two-pass method failed. A method that does it has not solved the problem, only relocated it
  to the join.

## 4. Selection

Selection and blending are **separate concerns with separate constants** — their conflation
(one 46° threshold read at six sites) is the clearest documented defect of the old code and is
not to be reproduced.

- **`min_angle = 46` is the crease classifier, and its only use.** Which edges are features vs.
  tessellation seams is decided from the mesh alone — never from `$fn`/`$fa`/`$fs`, which a
  solid does not carry (promise 2). The 46° default has a measured derivation
  (`ACCEPTANCE.md` promise 2) that carries forward intact; it is kept, not re-litigated.
- **Brush is a solid, not an argument** — the only handle OpenSCAD offers over an imported
  mesh. Brushes compose for free (union to widen, difference to carve exceptions).
- **Default (no brush) is the whole model**, i.e. every edge that `min_angle` classifies as a
  feature.
- **The selection pipeline is a three-way intersection:** feature edges (`min_angle`) ∩ brush
  region (location) ∩ sign filter (`convex`/`concave`, §1). The three axes are orthogonal —
  crease, location, edge-type — and compose by AND.
- **Predicate and partial coverage — decided, inherited from the current code.** The brush is
  intersected with the **1-D spine (the edge)**, not with the blend volume, giving keep-intervals
  in edge parameter (`AUDIT.md` §2 step 6). Where the brush ends, **the edge is treated as if it
  stopped there**: the blend is built over the clipped span and ends at the brush boundary exactly
  the way it ends at a real edge end. **The blend solid is never clipped.** The reason is
  decisive: clipping the blend *geometry* by the brush volume would cut through tessellated arcs at
  positions that move with `$fn`, making the result tessellation-dependent; clipping the 1-D spine
  is `$fn`-invariant. This also means **there is no runout / taper-to-zero** — the blend ends like
  an edge-end, so no variable radius is introduced (consistent with §6).

## 5. Correctness and refusal — inherited, unchanged

These are properties of the feature and hold under any internal method.

1. **Validity (promise 1).** Any input yields a closed orientable manifold, or the offending
   edge is left unblended and a warning names it. **A false refusal is the safe error; a false
   acceptance is the defect.**
2. **Classification is a property of the mesh (promise 2).** As §4. `$fn`/`$fa`/`$fs` affect
   only the tessellation of the arc surfaces the operator *builds*, never which edges are
   selected.
3. **Named refusal (promise 3).** A size that does not fit is dropped and warned, never
   silently clamped or silently discarded. Other edges on the same model are unaffected.
4. **Seams are acceptable (promise 4).** A visible line where two blends meet is a valid
   solid, not a smooth corner, and is not a defect. The bar is validity, not smoothness.
5. **Every operator accepts any solid, including its own output (promise 5).** This is §3.
6. **When the operator cannot build a valid blend at an edge, it skips that edge and warns** —
   and the skip decision is made from measured geometry (e.g. "the blend would reach past the
   material it is meant to sit on by 0.05 mm"), never from a guessed constant. `min_angle` is a
   classification input only (§4); it is not a stopping rule in the construction.
7. **All four cases skip the same way.** An additive blend punching through a thin wall and a
   subtractive blend crossing into a neighbour are the same failure — reaches geometry it was
   not derived from — detected by the same measurement. (`discussion.md` §6.)

### 5a. The correctness contract — what B1 guarantees, and what it does not

Stated positively, because B1's local surgery (§7a) makes it code-reviewable rather than a hope —
this is the guarantee a boolean kernel *cannot* give, since a kernel reruns the whole mesh:

- **(a) Local manifold preservation.** If the faces and edges the operator *touches* are locally
  manifold, the edit introduces no non-manifoldness. This is provable by inspecting the
  patch-and-sew code (§7a), not by measuring output.
- **(b) Pass-through.** Geometry the operator does not touch is emitted verbatim — *even if it is
  non-manifold*. The operator does not validate or repair the rest of the user's mesh, and does
  not need clean input to leave the untouched part intact.
- **(c) Bounded touch region.** The operator modifies nothing beyond a distance from each selected
  edge that is a function of `r`/`t` and the edge angle. The user can therefore reason exactly
  about what is safe: past that bound, the mesh is untouched.

What is *not* claimed: correctness of geometry the user's input placed within the touch region but
that the operator did not derive from (a second feature closer than the bound). That is the case
skip-and-warn (§5.6) exists for. The contract is (a)+(b)+(c) plus skip-loudly — not "valid on any
input regardless of what is nearby."

## 6. Non-goals

- **No variable radius along an edge** — one size per invocation. (Runout, if adopted in §4,
  is the degenerate exception and must be called out as such, not smuggled in.)
- **No differing radii meeting at a corner within one invocation** — use nesting.
- **No provenance-based classification.** `Tri::originalID` exists and is deliberately
  *reported, not used to reject* (`AUDIT.md` §6.2): a tool whose behaviour depends on how its
  input was built cannot be handed an imported STL. Any classify-by-provenance idea is already
  declined.
- **No dependence on clean input.** Input may be non-manifold, self-intersecting, badly
  oriented (`discussion.md` §2). The operator tolerates anomalies; it does not require them
  absent.
- **Experimental gate and Manifold-build requirement carry over** as availability constraints
  (`ACCEPTANCE.md` Availability), not requirements to change.

## 7. Internal method — rewrite, swept-tool internals killed

**Governing construction principle (owner, 2026-08-07): approximate and explainable over exact
and pure.** What is adopted from the redesign note is not its accuracy arithmetic but its
*character* — every construction step must be statable in one plain sentence ("slide the contact
under the surface until it is within `eps`"), even at the cost of mathematical exactness. Exact
closed-form placement is a non-goal. This is not a compromise for two reasons:

- **There is nothing to be exact about.** "The mesh is the surface" (`discussion.md` §3): the
  input is a fan of flat facets with no smooth object underneath, so a "pure" tangent-to-the-
  surface calculation is pure toward a surface that does not exist — which is precisely §1a's
  defect. Fitting the real facets approximately is *more* correct than constructing exactly
  against a phantom.
- **Approximation is made safe by refusal.** An impure step that overshoots is caught by the
  global validity query and refused with a name (promise 1). The error-bounding burden a pure
  system would carry analytically is instead discharged by the measurement. You are permitted to
  be approximate *because* you refuse loudly.

Every choice below is decided by "which is simpler to explain and check," not "which is more
accurate," whenever those two pull apart.


**Owner decision, 2026-08-07: full rewrite (Path B).** The four `*_tool` builders and the whole
swept-tool assembly (stations, cells, seam covers, canal, wedge, beads) are **removed**, not
privatized. Two reasons, both decisive:

- **Reviewability.** The artifact carries 37 of 58 functions with no direct test and 10 tunable
  constants with no coverage (`AUDIT.md` §4). A shell-reshape over those internals (the
  privatize-and-orchestrate option) would not pass review, and would re-import the untested
  patchwork wholesale. The rewrite is built **test-first**, so the mechanism-inventory-without-
  coverage problem does not recur. This is a requirement of the rewrite, not an afterthought.
- **Junctions are structural.** The two surviving defects are artifacts of assembling the
  result from independently-built pieces — see below — and a per-edge local construction cannot
  fix them by design. Only a rewrite removes them.

**Why the rewrite removes the junction defects (the point that justifies the cost):**

- `rib_into_boss`'s **membrane** is two hull cells of one chain abutting on a shared section
  plane, leaving a duplicate opposite-oriented triangle pair. It exists *only because the tool
  is a union of per-segment cells.* No cells ⇒ no membrane.
- `refused_neighbour`'s **sliver** is two separately-built beads tangent to a common wall and
  therefore to each other; equal `eps` offsets never separate them from each other. It exists
  *only because there are two bead solids to coexist.* No separate beads ⇒ no sliver.

A method that produces the blend as **one globally-consistent surface** has no two pieces to
leave a membrane between and no two beads to leave a sliver between. The defects are not fixed;
the construction that manufactures them is deleted.

**The correctness contract is stated positively in §5a** — local manifold preservation,
pass-through of untouched geometry, and a bounded touch region — a guarantee a boolean kernel
cannot give. B **relocates** the junction problem from *unsolvable* (two tangent beads) to
*solvable-and-code-reviewable* (author one manifold patch over the vertex star), which
`discussion.md` §8 names as the hard part: *you own validity.*

**Method committed: B1 — direct mesh edit / topological bevel** (`discussion.md` §8). Owner
decision, 2026-08-07. Lift faces near selected edges, insert the blend strip, sew incident strips
into one patch at each vertex star (§3a). Cost linear in *selected edges*, not model size. Deletes
the boolean and with it epsilon overshoot, coincident-face fragility, and batch-boolean cost.

**B2 (morphological offset) is rejected**, not parked: dilate-then-erode acts on the *whole*
surface, so it rounds geometry the user never selected. A selective, brushed feature cannot be
built on a global operator that has side effects outside the intended region.

**Placement math is replaced wholesale by the redesign note's search** (`archive/2026-08-06-
redesign-unsolicited.md`). All tangent-plane / averaged-crease-normal construction is removed.
What ports to B1, and what does not:

- **Ports (the placement engine, method-agnostic):** §3a per-edge contact search (fit where the
  contact lands on the real facet, not from a crease-averaged plane — the same principle as
  "the mesh is the surface"); §3b the `r`→`t` fixed point; §3c derive the arc from *contact*
  normals with the `|s − s'|` self-check as a free validity residual in millimetres; §3e adaptive
  station placement along the edge; §8 the spatial index, which also answers the refusal query
  (§5.6/§5.7).
- **Does not port (swept-tool machinery, deleted with the beads):** §3d "C, the interior point"
  (there is no wedge interior in B1 — the blend surface *is* the arc, sewn to the lifted faces);
  §6 seam covers / canal / overhang / wedge−canal.

**Neither document solves the sign-transition junction (§3a of this file).** The redesign note's
§5 states outright that its per-edge search is local and junctions "collectively conflict"; the
vertex-atomic patch is therefore **net-new design in B1**, not something the ported placement math
provides. The redesign fixes single-edge placement *accuracy*; the vertex-star patch fixes the
convex↔concave *crux*. They are separate work items, and the second is the harder one.

**The ported search is itself unproven** — the redesign note's own §9.1/§10 kill test (does the
search separate real blends from the 0.117·R artifact floor on curved walls?) was never run to
completion. It goes into the spike (§9.1) *before* B1 is built on it.

**Decision input carried over:** the Manifold boolean returned a valid mesh on only 2 of 40
identical invocations of `rib_into_boss` (`ACCEPTANCE.md` A1) — the reason the gate measures
each cell three times. B1 (no boolean) plausibly removes this flakiness entirely. **Until the
chosen method is *shown* deterministic, the "3 renders per cell, worst outcome" rule stays.**

## 7a. How B1 edits the mesh — no boolean, no sign operator

The result must *look* union'd (fillet/bevel) or differenced (round/chamfer) with a tool, but B1
produces it by *local* surgery — not a global boolean.

**The unsolved crux: how the blend seam meets a tessellated wall.** The crease between two stations
may be a clean line, but the **contact line at setback `t` lives on the wall**, and between two
stations the wall is arbitrary geometry — many facets, not flat, not one segment, concave and
convex stretches. Reconciling the blend with that surface is unavoidable work, and there are only
three places to put it:

1. **Global boolean** — what the swept tool did; the kernel absorbs the tessellation. Rejected:
   global, and the 2/40 nondeterminism (§7).
2. **On-surface contact + conforming re-triangulation** — place the contact *on* the surface and
   cut along the wandering offset polyline, walking facets. Deterministic and local, but it is
   exactly the tessellated-surface solve that overshoot exists to avoid — fiddly on curved walls.
3. **Overshoot + *local* intersection** (leading candidate) — keep the redesign note's
   under-surface tips (T1A/T2A/T1B/T2B, and the interior point) and the simple chord/arc patch, so
   a straight chord between two below-surface endpoints stays under the messy wall regardless of
   its shape; then replace the *global* boolean with a purpose-built clip of the blend patch
   against **only the incident facets**. This is what computes "where the patch exits the solid,"
   locally and (built purpose-specific) deterministically. It preserves almost all of the redesign
   construction and keeps overshoot's robustness — the least invention.

So the earlier "contact on the surface, no intersection ever" was wrong: on a curved wall the exit
curve must be computed somewhere. The real question is **global vs. local**, not boolean vs.
no-boolean — and overshoot has a robustness rationale independent of the kernel (it makes the
inter-station chord immune to the wall's tessellation). This fork is resolved by the spike (§9,
step 0), not pre-decided here. Option 3 is the working assumption.

**The seam is always closed — bridge, don't refuse (owner decision, 2026-08-07).** The operator
acts boolean-like: it produces a valid solid even where the blend is locally ugly, rather than
declining an edge. Three settled rules:

- **Overshoot is a fixed small `eps`, never deepened.** The only adaptive response to a chord that
  rides over a concavity is to **subdivide** (add a station), up to a subdivision limit. Depth is
  not a knob.
- **Far-side punch-through is harmless — no assumption needed.** The local clip considers only
  *same-side* facets: those whose normal agrees with the incident face's outward normal `n`
  (`normal·n > 0`), within the locality bound. The far side of a thin wall is a different surface
  with normal ≈ `−n`, so it is never in the clip set. An `eps`-overshot tip that pokes through a
  thin wall therefore interacts with nothing there — no comparison, no spurious intersection, no
  artifact; the blend seats against the near surface as if the wall were thick. (This *replaces*
  the earlier "assume eps never exits the far side" caveat with a guarantee: because we do not
  boolean, we simply never look at the far side.) The normal side-filter is also what selects which
  facets a chord is tested against for the under-surface check above.
- **Residual finger case, and an `eps`-free option.** The side-filter is not unique: within the
  overshoot reach a thin "finger" (solid–gap–solid) could present a *second* same-facing face past
  a gap. Judged rare; left unprevented by default. A cleaner alternative removes it *and* makes
  `eps` irrelevant: instead of placing a tip at a guessed `eps` depth, **cast a ray from empty
  space on the outward (`+n`) side into the solid and take the FIRST surface hit** as the contact —
  nothing behind the first hit is consulted, so fingers and gaps are moot and there is no depth to
  guess. This is a standard AABB first-intersection query (CGAL `AABB_tree::first_intersection`,
  `trimesh` nearest-hit, likely Manifold's Collider), so it should come essentially for free from
  whatever spatial index B1 uses. Adopt it if cheap; otherwise keep `eps` + side-filter. The spike
  decides based on the chosen library (point-first-hit is free; clipping the 2-D patch means
  querying at seam sample points — cheap, but confirm it is not fiddly before committing).
- **At the subdivision limit, bridge.** If a chord still rides over a concave patch after
  subdividing to the limit, drop a **direct bridging surface back to the model surface** to close
  the gap. The result is a valid closed manifold with a local "fingernail-catching" artifact —
  exactly what a boolean leaves there — never a hole and never a refusal.

The crossing + endpoint-inside test (a segment is inside a closed solid iff one endpoint is inside
and it crosses no boundary polygon, redesign §8; sound on locally-manifold input) keeps its role,
but that role is now **"subdivide, then bridge,"** not "refuse." This is stronger than
detect-and-refuse — the output is *always* a valid solid (promise 1 + promise 4: an artifact/seam
is acceptable, the bar is a valid solid) — while keeping the locality and determinism a global
boolean lacks. Convex/flat/smooth walls (the majority) seat with `eps` and no subdivision; only
concave patches within `t` reach subdivision, and only the worst of those reach the bridge.
(Reaching into a *separate* feature — §5a(c) — is a different case, still open: bridging there could
fuse geometry that should stay apart; treatment TBD, not resolved by this rule.)

Given a resolved seam, **add-versus-remove is not an operation — it is where the patch lands**,
which follows from the edge's sign:

- **convex edge** → the arc sits *inside* the original corner → material removed (reads as
  `difference` with a round tool).
- **concave edge** → the arc caps the valley from *outside* the corner → material added (reads as
  `union` with a fillet tool).

So a fillet and a round are the *same surgery*, differing only in which side the arc bulges — which
is why one `fillet()` handles both signs with no sign operator. And whichever seam option (1–3)
wins, the reconciliation stays **local** — confined to the incident facets of the selected edges,
never a global pass over the whole mesh. That locality is what makes cost linear in selected edges
and what makes the bounded-touch guarantee (§5a c) hold.

At a **vertex** where selected edges meet, the per-edge strips leave a gap (or overlap) at the
shared vertex; the **vertex-star patch** (§3a — corner cap for one sign, saddle for mixed) closes
it and is sewn to the strip ends. This is the only part that is not per-edge, and the mixed-sign
saddle is the net-new piece.

**Validated by spike step 0 (2026-08-07), option 3 — evidence + an invariant argument.** A standalone
prototype (build → independent verify → owner check, 4 rounds) gave empirical confidence on jittered
faceted geometry: the overshoot + local clip produces a manifold, deterministic, local seam; poke
failures are genuine and localized; the **sub-interval bridge is bounded** — added DOF flat
(constant = 3 across a 28×–100× background-density sweep) when the wall is refined *away* from the
concavity, growing only with the concavity's own tessellation. Every build watertight.

But the generality is **analytical, not by enumeration.** The reconciliation reads only three local
inputs per edge — the contact polyline P, the incident facets F (positions + outward normals), and
the crease-side stations — and its only operations (poke sign = `(chord − nearest_facet)·normal`;
insert P's poking nodes; fan-triangulate stations→P) consult **nothing beyond P, F, and normals**.
Global wall curvature is never read, and fan-triangulation needs no planarity, so manifoldness,
locality, boundedness, and determinism depend only on *local* facet geometry — identical in single-
and double-curvature meshes, and identical in synthetic vs imported meshes. So the single-curvature
synthetic spike was **sufficient**: a dome would run the same code on the same primitives.

Genuine residuals (analytical, not "untested cases"): (i) the seam stitch *triangle* count is
O(fine nodes under the span) — inherent to coarse-to-fine stitching, bounded by the model's own
tessellation, acceptable; (ii) the reconciliation *assumes* a valid P and locally-manifold incident
facets (§5a) — that assumption is the thing to uphold. The one input that IS curvature-dependent is
**producing P**: the spike's planar `z=t` section is extrusion-specific; on a dome, setback `t` is a
surface-distance locus, so P must come from a facet-walk / per-station ray. Extracting P is the
contact-search problem — **spike step (a)** — not the reconciliation.

## 8. Acceptance oracle — carried over intact

`fillet-bench/` is architecture-independent and is the whole reason a rewrite is safe to
attempt: it validates a topological bevel exactly as it validated the beads.

- `sweep.sh`, `mesh.py` (corrected criterion), `expect.txt`, the 28 models — unchanged.
- **Every cell measured at least three times, aggregated to the worst outcome** (A1). Not
  optional until the chosen method is shown deterministic.
- **Every verdict quotes the vertex count beside it**, against a plain unblended render of the
  same model. A "pass" earned by building less geometry is the failure mode that killed two
  prior attempts (redesign §9.2). This rule is non-negotiable.
- Refusal warns and names its edge (A3). Provenance invariance (A2) still holds — the
  classifier reads only the mesh.

**The oracle has a blind spot at mixed-sign corners, and it must be covered.** Measured
2026-08-07: `lbracket`, `box_step`, `rib` and `pocket` all have convex+concave vertices and are
**VALID at every radius** (A1: manifold, χ=2, genus 0) — yet the sheet-3 renders show the mixed
vertices built as crude pinched facets, not saddles. A1 cannot see this; only A5 (human-reviewed
junction sheet) does. Therefore **mixed-sign acceptance is not validity-only** — the saddle patch
(§3a) must be judged by the visual sheet or a sharper corner metric, or a bad saddle "passes."
This is the concrete reason the rewrite is justified even though these cells are already "valid":
the current output is valid and wrong.

## 9. The spike gating the next step

**The spike — three things to de-risk before any assembly is built**, in order. The method is
committed (B1, §7); the spike does not choose it, it proves the unproven foundations:
   - **(0) A tractable, deterministic *local* seam — and a bridge that keeps it solid. PASSED,
     2026-08-07 (§7a).** Established over 4 build→verify rounds on jittered faceted geometry: the
     overshoot + local clip gives a manifold, deterministic, local seam; poke failures are genuine
     and localized; the sub-interval bridge is bounded (added DOF flat vs background density,
     growing only with the concavity's own tessellation); every build watertight. Generality is
     **analytical**: the reconciliation reads only local inputs (contact polyline, incident facets,
     normals) and never global curvature, so it is curvature- and source-invariant — the
     single-curvature synthetic spike is sufficient, not a case-limited result (§7a). The only
     curvature-dependent input is producing the contact polyline P (planar section is
     extrusion-specific; a dome needs a facet-walk) — and that is step (a)'s job, not the
     reconciliation. This retired the primary gate (seam intractability = B1 is dead). Steps (a) and
     (b) remain and are now the active gates.
   - **(a) Contact placement — GUARANTEED by construction, not a gate.** *Retired 2026-08-07.* The
     "0.117·R artifact-floor separation" kill test was a **holdover from the old architecture** and
     does not apply to B1: that floor was the level of *boolean* mesh artifacts, and the separation
     test gated a *ball-seat size gate* — B1 has neither (no boolean; no seat gate). Placement is a
     **per-tip 1-D search** (binary search on the section-plane angle, or first-hit ray) = "walk
     setback `t` along the wall surface from the crease," which always has a root, so the tip lands
     on the surface by construction (foot-off ≈ 0 driven, not measured). Curvature never threatens
     landing. The only non-landing cases are *detected boundary cases*, not separation problems:
     **off-face** (wall ends before `t` → refuse/handle) and a **non-monotone predicate** (groove/
     S-bend within `t` → verify the returned tip directly, subdivide/bridge on failure). Between
     samples, curvature error is bounded by two density floors (`$fn`-like cap on station spacing
     along the sweep; arcSegments across the section) and by `r/R` (negligible for r ≪ R; the r ~ R
     regime is the crowding-refusal case). Nothing here needs a spike.
   - **(b) The vertex-star saddle patch (§3a) at a mixed convex+concave vertex — DEFERRED TO LAST
     (owner, 2026-08-07).** Net-new and the actual reason for the rewrite, but it is the **final**
     step, not the first. Build everything else end-to-end first — operators, seam reconciliation,
     single-sign junctions (the corner-ball idea), contact search — and **leave mixed-sign vertices
     without a patch for now** (a documented gap: those corners stay crude/unclosed until the saddle
     lands, matching or improving on today's behaviour, which is already valid-but-wrong there).
     When taken up: the ladder is `lbracket` → `box_step` (planar, one isolated mixed vertex — get
     the saddle right here) → `refused_neighbour` (mixed + crowding + refusal) → `rib_into_boss`
     (mixed + unfilleted + curved arrival, the class-3 case, and the membrane that must vanish).
     Because all planar mixed models are already A1-VALID (§8 blind spot), **success is judged on
     the sheet, not on validity** — a valid crude facet is a failure. Judged on *a clean saddle on
     the sheet with a sane vertex count*, not a proof of accuracy order.
No design questions remain open. *Resolved:* the seam fork (§7a) — option 3 (overshoot + local
clip), proven by spike step 0; selection predicate / partial coverage (§4 — spine-clip, no runout);
the correctness contract (§5a); `fillet`/`chamfer` never interact so `r`/`t` need no reconciliation
(§2). One item is *open by choice, deferred*: the mixed-sign saddle (§9 b) is the last step.

## 10. Next step — build order

Spike step 0 (seam) is **proven** (§7a, `spike/`); (a) placement is **guaranteed, not a gate**
(§9 a). So implementation can begin. Build order:

1. `fillet()` / `chamfer()` operator skeleton + module registration (§1).
2. Per-edge contact placement (per-tip search, §9 a) + seam reconciliation (port the algorithm from
   `spike/`, §7a option 3) → run against `fillet-bench/`, quote vertex counts.
3. Single-sign junctions — the corner-ball idea (§3a), re-expressed in B1.
4. Selection (spine-clip + `convex`/`concave` + `min_angle`, §4) and the crowding/neighbour refusal
   (§5a c — the one open detection problem).
5. **Last: the mixed-sign saddle (§9 b).** Until then, mixed-sign vertices are left unpatched — a
   documented gap, judged on the visual sheet when taken up.

Each step re-swept against the bench; mixed-sign judged on the sheet, not A1.
