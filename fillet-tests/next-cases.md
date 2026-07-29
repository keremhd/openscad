# Cases wanted next

The queue in [`junction-cases.md`](junction-cases.md) is spent — every case in it
is built and its outcome recorded. This is the next queue, written the same way:
the point of each entry is to **find out what happens**, and an answer recorded
in `expectations.txt` is worth more than an untested corner nobody has looked at.

Read [`README.md`](README.md) first for the case-file contract, the four checks
and what `ref` / `none` / `drop` mean. Everything here assumes it.

**A case here is expensive and a unit test is not.** Every check in this suite
dilates through CGAL's Nef kernel and costs seconds; `FilletBuilder_test.cc`
answers a question about a count in milliseconds and pins it exactly. So an entry
earns a place here only if its answer is a *shape* — something the six columns
show and a number does not. An entry whose answer is "how many edges were
selected" belongs in the unit tests, and most of the first draft of this file
turned out to be that. Two sections below record what was withdrawn and why,
because a queue that only ever grows stops being a queue.

Each entry is marked:

- **PROBED** — run by hand against the builder, with the numbers below. The case
  is worth writing to pin the behaviour, not to discover it.
- **PREDICTED** — reasoned from the code and not run. Expect some of these to
  come out clean, and say so in the case header when they do, exactly as Group A
  of the last queue did.

Nothing here should crash. If one does, that is the finding — report it as CRASH
rather than tuning the case until it stops.

---

## Group 3 — curved creases, and junctions on them

Every junction in the suite is polyhedral: a vertex where straight spines meet.
Nothing tests a junction whose incident creases are curves, and that is *still*
true after the two cases below, which is what makes it the one real gap left.

### `case_pipe_tee_fillet` — **BUILT**

The suite's first curved crease. A branch pipe stubbed into a run pipe: the seam
is a closed space curve whose dihedral varies continuously along it, so every
station asks the frame, the setback and the seated ball a different question.

**Clean at `r = 2` and `r = 4`**, no expectation line. One closed chain of 48
segments, no station refused, and the bead covers the whole curve — the seam
spans `z 23..37` and the tool measures `z 21..39` and `z 19..41`, seam plus
radius at both ends.

### `case_pipe_tee_equal` — **BUILT, and it does not contain the junction**

Equal radii, branch crossing rather than stubbing, so the two seam loops cross
each other. **The crossings are not junctions and cannot be made into them.**

The reason is in the algebra, and the first draft of this file had it backwards.
With the branch along X the seam solves to `(z-Zb)^2 = x^2 - (Rr^2 - Rb^2)`. A
narrower branch makes that negative near `x = 0`, so the loops start only where
it turns positive and never touch — a branch at 9 against a run at 10 does not
cross transversally, it does not cross at all, and its seam comes back as two
closed chains of 80 whose nearest approach to the crossing plane is the 4.36 the
constant predicts. Only equality kills the constant and lets the loops meet.

And equality is exactly what destroys the junction. Where the ellipses meet, the
two cylinders share a tangent plane, so the seam's dihedral runs to zero on the
way in, falls under the crease threshold well before it arrives, and is cut there
like any other shallow feature. Measured: every concave edge within 3 mm of the
crossing plane turns by less than the threshold, and the chains come back as
**four open arcs**, not two crossing loops. Tessellating finer does not recover
them, because tangency is the reason the loops cross in the first place.

Both halves are pinned in `FilletBuilder_test.cc` — "chains: two seams that cross
do so where neither is still a crease". The case exists for the half a count
cannot carry: what four runouts converging on one point look like. It is red on
`sandwich` for the pocket cases' reason, with the useful twist that the same
solid dilates when the branch is rotated onto the other axis; see
`expectations.txt`.

### `case_two_bosses` — the curved junction, and a shape that can hold one

**PREDICTED**, and the replacement for what `case_pipe_tee_equal` was supposed to
give. A junction on a curved crease needs creases that meet **at an angle**, and
a tee cannot supply that at any radius. Two overlapping bosses on a plate can:

```openscad
module case_model() {
  cube([60, 40, 6]);
  translate([22, 20, 6]) cylinder(r = 10, h = 20, $fn = 48);
  translate([36, 20, 6]) cylinder(r = 10, h = 20, $fn = 48);
}
```

Each boss has a base ring — a closed curved concave crease — the two rings cross
at two points, and a straight concave crease runs up the groove where the two
cylinders meet. At each crossing: two curved arcs and one straight spine, meeting
at a real angle. Valence three rather than four, which is not the interesting
part; the interesting part is that two of the three spines are not lines.

- **watch for:** whether the two rings survive being cut into arcs, and what the
  corner cell does where a curved spine meets a straight one.
- the bosses overlap by 6 of their 20 diameter, so the groove is well clear of
  tangency. Slide them to touch and this becomes `case_pipe_tee_equal` again.

### `case_rib_into_boss`

**PREDICTED**, and worth building only after the one above, which it overlaps.

```openscad
module case_model() {
  cube([60, 40, 6]);
  translate([40, 20, 6]) cylinder(r = 10, h = 20, $fn = 48);
  translate([10, 18, 6]) cube([25, 4, 20]);
}
```

Half of this belongs in the unit tests: "does the ring's chain survive being
interrupted" is `buildChains` returning arcs instead of a loop, which is a count.
What needs the picture is the corner cell where the rib's straight foot creases
run into the ring — and `case_two_bosses` shows that with one fewer moving part.
Build this if the bosses leave a question open.

## Group 5 — brushes past the half-chain case

**All PREDICTED.** `case_brush_halfchain` covers the one thing selection had to
get right — a flat cap where the spine crosses the brush, not a scooped one — and
the unit tests pin the graze guard, the empty selection, the two corner rules and
the slanted brush face (the cap stays square to the crease; the brush's own angle
does not reach the result). These are the configurations a picture would say more
about than a volume does.

- **`case_brush_corner_partial`** — a cube under `round_tool` with a brush over
  one corner that stops partway along each of its three edges, and a variant
  lifted so it misses one edge entirely. The first keeps its corner cell and
  overshoots the brush by what truncation takes; the second builds no corner and
  caps two beads flat. Both are deliberate, and both look wrong until you know
  which one you are seeing.
- **`case_brush_negative`** — `difference() { big_brush(); keep_sharp(); }` as the
  brush, which is the whole argument for brushes being ordinary CSG. Nothing in
  the operator knows it happened, so this is a case about the *idiom* reading
  correctly, not about new code.
- **`case_brush_ring_arc`** — an arc of the hole-mouth ring. A closed chain whose
  selection wraps is contiguous across the wrap by construction; one that does
  not wrap is two caps on the same ring. Neither has been looked at.

## Moved to `FilletBuilder_test.cc`

Each of these was queued here as a case and each turned out to assert a count, so
it is answered where a count is cheap and exact. They stay listed because the
reason they are not cases is worth having written down — and because one of them
found something much larger than the question it was asked.

- **the `$fn`-derived threshold, in the direction nothing covered.** The two
  existing threshold tests only check that a tessellation seam is never read as a
  crease. The other direction — a crease the shape really *has*, shallower than
  the facets the caller happens to be working at, silently not selected — had no
  coverage at all. A gable prism whose apex turns 30 degrees and whose shoulders
  turn 75 loses the apex at `$fn = 8` (threshold 67.5) and keeps it at `$fa = 12`
  (18), with nothing said and only the shoulders rounded. `min_angle=` is the way
  out and has to be: the threshold cannot both keep a cylinder smooth and pick up
  a crease shallower than that cylinder's own facets.
- **the exact tie.** A model tessellated at `$fn = 16` and read by a caller at
  `$fn = 24` puts the facet angle exactly on the threshold. The comparison is
  `dihedral < threshold`, so all sixteen seams should be taken; **twelve are**,
  and nothing in the model distinguishes the other four. The count is pinned as
  the float noise it is — a strict inequality is not the fix, it would only move
  which side of the tie is arbitrary.
- **a solid with nothing to round.** A sphere selects zero of its 768 two-face
  edges and builds an empty tool. Correct, and worth pinning, but it needed a new
  harness `kind` to live here and its whole assertion is `feature == 0`.
- **re-filleting**, which was queued as a threshold question and is not one. The
  arc facets do fall under the threshold; that part is uninteresting. What a
  rounded cube comes back with is **hundreds of creases of both signs on a solid
  that is convex everywhere** — 650 feature edges, 383 of them concave, at
  `r = 5` on a 40 mm cube. The shape is right: it is within a fraction of a
  percent of the exact rounded cube's volume, and the exact one — a hull of eight
  spheres — classifies at zero. The *mesh* is not. The beads meet the flat faces
  and each other tangentially, and the slivers that leaves carry creases on edges
  four orders of magnitude below the part.

  This is the same defect `expectations.txt` already blames for the pocket cases
  refusing to dilate, measured from the other end, and now seen a third time in
  `case_pipe_tee_equal`. **Reducing tangential contact is the one change that
  turns all three green, and it is the most valuable thing in this file.**
- **non-manifold input.** Two cubes sharing one edge, and two sharing one vertex.
  Neither crashes. The shared edge has four incident triangles, is counted
  non-manifold and never reaches the selection, so its two cubes are rounded as
  if they never touched. The vertex-sharing pair is subtler and comes out
  entirely clean — sharing a point makes no edge non-manifold — so nothing marks
  it as degenerate at all except the genus.

## Withdrawn, with the reason

Deleting these is half the point of the review that produced this revision. A
case that pins something already pinned costs a CGAL dilation on every run and
tells its next reader it is load-bearing.

- **`case_round_past_boss`** — probed and already correct, and `size: a blend
  wider than the face it must meet is refused` and `size: a cube's rounds are
  limited by the edge across the face` already pin `OffFace` from both
  directions. The only new claim was that the off-face test reads a face's
  outline and not merely its plane, which the L-shape test already exercises.
- **`case_boss_base_oversize`** — covered twice over, and the premise was wrong.
  `case_boss_base_fillet` runs `r = 24` on a boss of radius 12 as a `ref` variant
  that passes, and `FilletCompare_test.cc` carries "a boss base fillet larger
  than the boss still matches". A boss *base* ring is concave outward: its
  envelope opens away from the axis and cannot collapse through it. The collapse
  this entry described needs a concave ring on the inside of a bore, which is a
  different shape and was not the one proposed.
- **`case_crease_near_180`** — `case_junction_flat_apex` is already a 1.9-degree
  knife edge whose setback is some sixty radii, and its permanent `sandwich`
  failure is documented for exactly that reason. A second shape at the same ratio
  adds a second red line and no information.
- **`case_crease_near_0`** — bracketed on both sides already, in `spine: a slit
  too narrow to roll a ball into is skipped, not solved`: a 60-degree groove is
  ordinary geometry and a half-degree slit is refused. Five degrees is an
  interpolation between two pinned endpoints.
- **`case_coincident_faces`** — the union deletes the shared face, so the merged
  mesh is one box and the case would test Manifold rather than the fillet. What
  the operator does with coplanar input is already in `surfaces: a seam joins two
  triangles into one wall, a crease does not`.

### And the whole of Group 2 — obstacles that are faces, not creases

This was the most promising-looking group in the file. It has no reachable
failure, and the arithmetic is worth keeping so nobody re-queues it.

`case_overhang_fillet` probed empty: a cantilever that blocks the seated ball
puts no tool volume anywhere, because the bead is the sliver *between the corner
and the ball* and only the ball ever reaches the obstacle. `case_obstacle_in_the_bead`
was the sharpened version — put the obstacle inside the bead's own footprint
rather than merely inside the ball — and the bead has almost no footprint to put
anything into. For a right-angle crease its area is `0.215 r^2` against the
ball's `0.785 r^2`, and its thickness away from the corner falls off fast:

| distance along the face | bead thickness |
|---|---|
| `0.25 r` | `0.34 r` |
| `0.50 r` | `0.13 r` |
| `0.75 r` | `0.03 r` |

Probed with a pier hung 1 mm above the floor and 5 mm from the wall, at `r = 8`:
no warning, all four creases built, and **no tool volume inside the pier** — at
`x = 9` the bead reaches only `z = 5.58`, below the pier's underside at `z = 6`.
An obstacle that is inside the bead and not inside the model has to sit within a
fraction of a millimetre of a crease, which is not a part anyone makes.

So "the ball must be able to reach" does not belong in the size gate; the gate is
right to measure against creases and faces rather than against reachability, and
there is no case to write. The one place the arithmetic changes is an acute
groove, where the ball sits far up and the bead is genuinely long — and that is
`case_crease_near_0` territory, already pinned at 0.5 and 60 degrees.

## Harness gaps these keep running into

Not cases, but the reason several cases above have to be read by eye:

- **`sandwich` cannot see an under-fill.** It bounds how far the result may
  *stray* from the model; a bead that is too small strays nowhere. Every
  collapsed-bead and truncated-spine defect so far has passed it. A "fills"
  check — no point of the model within the tool's reach of a selected crease may
  be left unblended — would have caught the near-wall collapse and the needle
  runout on the day they appeared.
- **`sandwich` cannot see a tangency either**, for a different reason: CGAL will
  not dilate a result whose surfaces meet tangentially, so the check returns no
  verdict rather than a wrong one. Four cases are red for this and not one of
  them is red about its own geometry.
- **`drops` is per-node, refusal is per-crease.** A `drop` variant asserts the
  whole tool comes out empty, but the gate refuses one crease at a time, so a
  model with one bad crease and one good one can only be `none` today. That is
  why `case_near_wall_fillet`'s large variant is not a `drop`.

## What not to build

The list in [`junction-cases.md`](junction-cases.md) still holds — unequal radii
per edge, oversize radii as their own cases, and anything claiming a hand-written
reference for a blend that has no closed form. Add to it: **anything whose answer
is a count.** It goes in `FilletBuilder_test.cc`, where it is exact, free, and
does not have to be read off a picture.
