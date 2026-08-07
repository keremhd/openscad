# Independent verification — B1 seam spike, ROUND 4

The R4 logic lives in **`spike3.py`** (updated in place; confirmed by reading the
current file — new `poking_nodes`, sub-interval `plan_seam`, corrected metric
`bridge_added_verts = len(stations) - coarse_count`, and `locality_two_axis` /
`make_wall`). I re-ran it (numbers reproduce; whole run byte-identical across two
runs), read it in full, and re-measured everything with my own methods — a
hand-rolled edge counter, an independent z=t corridor-vertex count, and an extended
background sweep. Independent script: `verify4.py`.

**Headline: the R3 boundedness refutation is genuinely addressed.** Added seam
vertices are dead flat under a 100×+ background refinement (I pushed to 1025 nodes,
4× beyond their 257), and grow only with the dent. One honest caveat remains
(seam *triangle* count still scales with wall density), stated below — but the
specific bounded-vertex claim the verdict rule targets is CONFIRMED, not
rubber-stamped: I reproduced and extended the decisive sweep myself.

---

## Claim 1(a) — refine wall AWAY from dent, added corridor verts FLAT: **CONFIRMED (and stronger than reported)**

My **own** count of seam-corridor vertices (vertices at z=t, isolating exactly the
Sw corridor) across an extended background sweep:

| bg_nodes | total nodes | coarse cnt | my z=t corridor verts | added (=−coarse) | bg nodes in poking span | watertight (mine/trimesh) |
|---|---|---|---|---|---|---|
| 9 | 15 | 4 | 7 | 3 | 9 | True/True |
| 33 | 29 | 4 | 7 | 3 | 15 | True/True |
| 129 | 85 | 4 | 7 | 3 | 31 | True/True |
| 257 | 159 | 4 | 7 | 3 | 55 | True/True |
| **513** | 307 | 4 | 7 | 3 | 101 | True/True |
| **1025** | 603 | 4 | 7 | 3 | 191 | True/True |

- **The background is genuinely refined, and relevantly so:** total nodes 15→603,
  and the number of fine background nodes *inside the poking coarse span* grows
  9→191. So refining is not a no-op and it is not being routed around the seam — yet
  the added corridor verts stay **exactly 3** (7 total − 4 coarse). Dead flat to
  1025 nodes; not slow growth. This is genuine boundedness of the added seam
  degrees of freedom, and it is the make-or-break number — CONFIRMED, independently
  reproduced with my own vertex count and extended 4× past their sweep.

## Claim 1(b) — refine the DENT, added verts grow with dent tessellation: **CONFIRMED**

My z=t corridor-vertex counts as the dent is refined (background fixed at 33):

| dent_nodes | my z=t corridor verts | added |
|---|---|---|
| 5 | 5 | 1 |
| 9 | 7 | 3 |
| 17 | 11 | 7 |
| 33 | 17 | 13 |
| 65 | 31 | 27 |

Grows monotonically with the dent's own tessellation (1→3→7→13→27), as claimed —
acceptable necessary fidelity: more concave detail needs more seam nodes. Combined
with 1(a), the verdict rule (flat on background AND grows on dent) is satisfied by
my own numbers.

## Claim 2 (of the request) — METRIC HONESTY: **CONFIRMED, with one caveat**

The metric `corridor_seam_verts = len(stations)` counts only the Sw (z=t) corridor
vertices; it excludes the retained fine wall/plate/back rings (W1 at z=t+1, PI at
z=0, PO/POB/BB/BT), which are full-resolution by design. My **independent** z=t
count matches the metric exactly at every sweep point (7 flat on axis a; 5/7/11/17/31
on axis b), so the metric genuinely isolates the corridor and is not counting the
wrong thing. The "flat 3" is not a trivially-small dent either: the dent has 9
nodes and 3 of them actually poke; refining the dent to 65 nodes yields 27 poking.

**Honest caveat (my own finding, not in the report):** boundedness holds for added
*vertices/stations*, NOT for the corridor's *triangle count*. My count of stitch
triangles touching the seam grows **44 → 1220** across the same bg 9→1025 sweep —
i.e. ~linearly with background density. This is inherent: `stitch_fan` connects the
coarse Sw edge to *every* fine retained-wall node (W1/PI) between stations, so a
watertight seam against a fine wall costs O(fine nodes) triangles regardless of how
few new seam vertices are added. So the correct characterization is: **the bridge
adds O(concave-feature) new degrees of freedom, but the seam mesh near a fine wall
is still O(wall density) in triangles.** That is arguably the right/only behaviour
(you cannot skip fine wall nodes and stay watertight), but "bounded" should be read
as "bounded new seam DOF," not "O(1) seam mesh."

## Claim 3 — SUB-INTERVAL LOGIC (shoulders coarse, only poking nodes inserted): **CONFIRMED**

At bg=129: coarse stations = [0, 26, 58, 84]; the bridge added **only [41, 42, 43]**
(y = 4.475, 5.0, 5.525 — dead centre of the dent, footprint [2.9, 7.1]).
`poking_nodes(26,58)` = [41,42,43]; shoulder nodes 27..40 and 44..57 stay coarse
(not promoted to stations). So it inserts fine stations strictly on genuinely-poking
nodes and leaves the non-concave shoulders coarse — exactly as claimed. (Only 3 of
the 9 dent nodes poke because the Gaussian's deep part is narrow; the 3 is the real
poking sub-interval, not an artifact.)

## Claim 4 — VALIDITY + DETERMINISM across the sweep: **CONFIRMED**

Every sweep point (both axes, incl. bg=513 and 1025) is watertight by my own edge
counter AND trimesh (0 boundary, 0 non-manifold, 0 orientation mismatch, χ=2, 0
duplicate/degenerate/zero-area faces — spot-checked across bg=9,33,129,257,513,1025
and dent=5..65). Whole `spike3.py` run is byte-identical across two runs, and the
two-axis sweep is canonically identical across two independent runs (my check).

## Claim 5 — previously-confirmed properties still hold: **CONFIRMED (no regression)**

- Convex no-poke seam watertight at 2 and 3 stations (0 boundary, 0 dup) — holds.
- Dent coarse no-bridge: open_spans = exactly the two poking spans (boundary 40);
  convex control at the same stations closes (boundary 0). Holes still poke-caused,
  not tessellation mismatch — holds.
- Mid-facet ray gap unchanged: node ~1.8e-15, mid-facet max 0.1028, mean 0.0373.

## Dome / side-filter: unchanged from round 3 — dome primitives port (82 facets
walked, deterministic contacts, 1.5 mm poke) but the full bridged dome-seam mesh is
still NOT assembled (scope limit); side-filter still trivial (axis-aligned box).

---

## Overall verdict

**The bounded local poke-aware bridge is now genuinely proven — for the added-seam-
vertex (degrees-of-freedom) metric — and I confirmed it with my own measurements,
extended 4× beyond the spike's sweep.** Added seam vertices are flat at 3 across a
background refinement from 15 to 603 total nodes (bg 9→1025), while the background
inside the poking span genuinely grows (9→191 fine nodes); they grow only when the
dent itself is refined (1→27). The sub-interval logic inserts stations strictly on
poking nodes and keeps shoulders coarse. Every build is a valid, watertight,
deterministic manifold. This directly and honestly answers the R3 boundedness
refutation — it is a real fix, not masking.

**One caveat, to not overstate it:** boundedness is in *new seam vertices/stations*,
not in seam *triangle count* — the stitch to a fine retained wall is inherently
O(wall density) in triangles (my count 44→1220 over the same sweep). That is
unavoidable given a fine wall and is a reasonable behaviour, but "the bridge is
O(1)" would be too strong; "the bridge introduces O(concave-feature) new DOF" is
the accurate statement.

**Does step 0 fully hold?** For the vertical-extrusion faceted geometry tested: yes
— tractable, deterministic, local seam (claims 1/2/5) + a poke-aware bridge whose
added DOF are bounded to the concave feature (claims 1a/1b/3), all valid and
deterministic (claim 4). Residual scope limits carried forward, now the main open
items: (1) the doubly-curved bridge is still primitives-only, never assembled — the
hardest case for the bounded bridge is untested; (2) geometry is synthetic, not the
bench STLs; (3) side-filter trivial; (4) the triangle-count caveat above. No bugs
found in `spike3.py` or `meshutil.py`; all produced meshes are valid manifolds.
