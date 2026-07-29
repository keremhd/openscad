# Cases wanted next

The queue in [`junction-cases.md`](junction-cases.md) is spent — every case in it
is built and its outcome recorded. This is the next queue, written the same way:
the point of each entry is to **find out what happens**, and an answer recorded
in `expectations.txt` is worth more than an untested corner nobody has looked at.

Read [`README.md`](README.md) first for the case-file contract, the four checks
and what `ref` / `none` / `drop` mean. Everything here assumes it.

Each entry is marked:

- **PROBED** — run by hand against the builder, with the numbers below. The case
  is worth writing to pin the behaviour, not to discover it.
- **PREDICTED** — reasoned from the code and not run. Expect some of these to
  come out clean, and say so in the case header when they do, exactly as Group A
  of the last queue did.

Nothing here should crash. If one does, that is the finding — report it as CRASH
rather than tuning the case until it stops.

---

## Group 1 — the feature threshold is tied to `$fn`, and nothing tests it

**PROBED, and this is the surprising one.** The angle that decides whether an
edge is a feature is derived from the caller's tessellation setting: it comes out
at **1.5x the facet angle**. Measured, on `cylinder(r = 10, h = 20, $fn = 17)`
under `round_tool(r = 1)`, reading the threshold straight off the builder's echo:

| caller's setting | threshold | convex edges selected |
|---|---|---|
| `$fn = 8` | 67.5° | 34 — the two rims only |
| `$fn = 24` | 22.5° | 34 — the two rims only |
| `$fa = 12` (the default) | 18.0° | 51 — rims **and** all 17 facet edges |

The rule itself is defensible — an edge is a feature when it turns more than the
tessellation's own facets do, which is what keeps a smooth cylinder from being
read as a fan of creases. What has no coverage at all is the consequence: **the
same solid gets different edges rounded depending on `$fn` at the call site**,
and both of the failure directions are reachable.

### `case_facet_threshold_shallow`

A shallow crease and a steep one in the same solid, swept across settings. The
roof apex turns 30° (a 150° interior), the two shoulders turn 75°:

```openscad
module case_model() {
  linear_extrude(40) polygon([[0, 0], [60, 0], [60, 10], [30, 18], [0, 10]]);
}
```

so at `$fa = 12` (18°) both are features, and at `$fn = 8` (67.5°) the shoulders
still are and **the apex silently is not** — a chamfer a user asked for and did
not get, with no warning, decided by a variable that was supposed to be about
smoothness. One model, both sides of the threshold, and the picture shows which
edge was taken.

- variants: one per setting rather than per size — `$fn = 8`, `$fn = 24`, default.
  The size stays fixed; it is the threshold being varied, and mixing the two into
  one case would hide both.
- **watch for:** which of the two shallow creases is picked up at each setting,
  and whether anything is said when none is. The interesting verdict is whether
  a tool that selects nothing should warn; today it is indistinguishable from a
  tool that was refused.

### `case_faceted_cylinder_round`

**PROBED.** A faceted cylinder under `round_tool(r = 1)`, at `$fn` either side of
the tie, all three measured with the caller at `$fn = 24` — so the threshold is
22.5° throughout and only the model changes. Rims are `2n` edges; anything beyond
that is the vertical facet edges being rounded too, which turns a prism into a
blob:

| model `$fn` | facet turn | selected (rims = 2n) | vertical edges taken |
|---|---|---|---|
| 12 | 30.0° | 36 (24) | all 12 |
| 16 | 22.5° | 44 (32) | **12 of 16** |
| 32 | 11.25° | 64 (64) | none |

The middle row is the one to pin. At `$fn = 16` under a `$fn = 24` caller the
facet angle equals the threshold **exactly**, and the tie breaks 12 one way and
4 the other — a partial, arbitrary selection that leaves four facet edges sharp
and rounds the rest. Nothing about the model distinguishes those four. Whatever
the fix is (a strict inequality is not it — it just moves which side of the tie
is arbitrary), the case is what stops it regressing.

- tool: `round_tool`, `"subtract"`, `none`, `size = 1`.
- **watch for:** the four unrounded facet edges in the picture, and whether the
  count is stable across runs.

### `case_smooth_solid_noop`

**PROBED.** `round_tool(r = 1) sphere(10, $fn = 32)` classifies **0 feature
edges**, one surface, and emits an empty tool — correct, and silent. Worth a case
mainly to fix the contract: an empty tool from a solid with nothing to round is
right, but it is byte-for-byte what an unimplemented operator produces. This is
the same argument that gave `drops` its warning requirement.

- kind: this needs a `drop` variant or a new one; `emits` would fail it for doing
  the correct thing, which is why it is listed here rather than written blind.

## Group 2 — obstacles that are faces, not creases

The size gate refuses a crease whose seated ball reaches another **crease's**
contact line. The obstacle in general is a **face**, and a blocking face need not
carry any selected crease within reach.

**Which sign can even have this problem** is worth stating, because I got it
backwards first and the probe corrected me. For a **concave** crease the seated
ball rolls in *air*, so a foreign face can sit in its way — that is the near-wall
case and the one below. For a **convex** edge the ball rolls *inside the solid*,
so the only thing that can block it is the solid's own thinness, and that is
already what the off-face test measures. There is no convex twin of this group.

### `case_overhang_fillet`

**PROBED, and weaker than it looks — build it to pin the negative.** A concave
crease with a cantilevered slab passing over it, attached far enough away that
its own creases are out of range of the ball:

```openscad
module case_model() {
  cube([60, 40, 5]);                       // floor
  cube([4, 40, 30]);                       // the wall carrying the crease
  translate([50, 0, 5]) cube([10, 40, 9]); // far pillar
  translate([8, 0, 10]) cube([52, 40, 4]); // slab, underside at z = 10
}
```

At `r = 8` the seated ball is centred at `(12, 13)` and the slab's underside
cuts 3 mm below that centre, so the ball is deeply blocked — and the gate says
nothing about this crease, exactly as predicted. But the tool puts **no volume
inside the slab at all** (measured: empty), because the bead is the sliver
*between the corner and the ball*, which hugs the corner while only the ball
itself reaches the obstacle. Blocking the ball is not the same as damaging
anything.

So what is actually wrong here is narrower than "the bead rolls through a face":
the surface built is no longer one a ball of that radius could sweep, which is a
statement about reachability rather than about the solid. Whether the operator
should care is a real question — it is the same question as whether a fillet you
could not machine is still a fillet — and it should be settled deliberately
rather than discovered.

- **watch for:** nothing in the slab (that is the pin), and in the picture, a
  bead whose arc implies a ball that could never have got in there.

### `case_obstacle_in_the_bead`

**PREDICTED**, and the sharpened version of the above: put the obstacle inside
the bead's own footprint rather than merely inside the ball. The bead spans
`x 4..12` along the floor at `r = 8`, so a rail floating at `x 6..9, z 6..8` —
attached far away, touching neither floor nor wall, therefore carrying no crease
within reach — is *in the material the bead claims*.

A union tool cannot gouge it, so the failure is not a gouge: the bead and the
rail simply weld into one lump, and the blend surface that should have been
interrupted by the rail is not. That is the case that decides whether "the ball
must be able to reach" belongs in the size gate at all.

### `case_round_past_boss`

**PROBED — already correct.** A boss standing closer to the plate's edge than the
round's own reach:

```openscad
module case_model() {
  cube([40, 40, 10]);
  translate([7.5, 20, 10]) cylinder(r = 6, h = 15);   // 1.5 from the x = 0 edge
}
```

At `r = 2` this warns `the blend would leave the surface it is meant to meet, by
0.371` and drops the chain, leaving no tool volume over the boss footprint. So
the off-face test reads the face's actual outline and not merely its plane — the
contact point lands inside the boss's footprint and is correctly judged off the
free surface. Write it as a regression pin, not as a hunt: it is cheap, it is
green, and the property it holds is easy to lose.

## Group 3 — curved creases, and junctions on them

Every junction case so far is polyhedral, and every closed chain so far has
constant curvature. Both of those are the easy half.

### `case_pipe_tee_fillet` — **BUILT**

The suite's first curved crease. A branch pipe stubbed into a run pipe: the seam
is a closed space curve whose dihedral varies continuously along it, so every
station asks the frame, the setback and the seated ball a different question.

**Clean at `r = 2` and `r = 4`**, no expectation line. One closed chain of 48
segments, no station refused, and the bead covers the whole curve — the seam
spans `z 23..37` and the tool measures `z 21..39` and `z 19..41`, seam plus
radius at both ends.

The correction worth keeping: a branch that *crosses* the run leaves two separate
seam loops, which is two easy cases rather than one hard one — hence the stub.
And there is no saddle in this shape, contrary to what the first draft of this
file claimed. The saddle needs the case below.

### `case_pipe_tee_equal` — the saddle

**PREDICTED.** Give the branch the *same* radius as the run and the two seam
loops stop being separate: they cross each other at two points, on the plane of
the two axes. Those crossings are junctions of valence four **on a curved
crease** — the only kind the suite has no case for, since every junction it owns
is a polyhedral vertex where straight spines meet.

```openscad
module case_model() {
  cylinder(r = 10, h = 60, $fn = 48);
  translate([0, 0, 30]) rotate([-90, 0, 0]) cylinder(r = 10, h = 30, $fn = 48);
}
```

- **watch for:** whether the crossings are found as junctions at all — chain
  building walks a crease until it meets another, and two seams meeting at a
  tangential crossing is the case where "meets" is hardest to decide.
- the equal radii make the crossing exactly tangential, which is the degenerate
  end of it. A branch at 9 against a run at 10 crosses transversally and is the
  easier variant; build both if the equal one turns out to be a knife edge.

### `case_rib_into_boss`

**PREDICTED.** A straight crease running into a closed curved one: a rib whose
end lands on a cylindrical boss standing on the same plate. The rib's two foot
creases and the boss's base ring meet at two junctions, and the ring is a closed
chain that is *not* closed once those junctions cut it.

```openscad
module case_model() {
  cube([60, 40, 6]);
  translate([40, 20, 6]) cylinder(r = 10, h = 20, $fn = 48);
  translate([10, 18, 6]) cube([25, 4, 20]);
}
```

- **watch for:** whether the ring's chain survives being interrupted, and what
  the corner cell does where a straight spine meets a curved one.

### `case_boss_base_oversize`

**PREDICTED.** A fillet whose radius exceeds the curvature it has to follow: a
boss of radius 5 with `r = 6` at its base. The bead's inner envelope collapses
through the axis, so no constant-radius blend exists — the concave-side analogue
of the size limit, and one the current gate may not express, since it measures
against other creases and faces rather than against the chain's own curvature.

- expect either a clean refusal (then it is a `drop`) or a self-intersecting
  bead. Both are worth knowing; only one is worth keeping.

## Group 4 — extremes and robustness

**All PREDICTED.** Cheap to write, and the kind of thing that turns up crashes
rather than wrong answers.

- **`case_crease_near_180`** — two plates meeting at 178°. The setback is
  `r*tan(89°)`, some 57 radii, so almost any size fails off-face. Check that the
  refusal is the reason given and that nothing divides by a vanishing normal.
- **`case_crease_near_0`** — a 5° V-groove. The seated ball sits ~23 radii down
  the groove; the mirror of the needle, on a concave crease.
- **`case_refillet`** — `fillet_tool` applied to a model that already carries a
  fillet. The arc facets are shallow, so they should fall under the threshold and
  be ignored — but see Group 1: that depends on the `$fn` in force, and a fillet
  tessellated at one setting re-read at another is exactly the collision.
- **`case_edge_only_contact`** — two cubes sharing exactly one edge, and a
  variant sharing exactly one vertex. Non-manifold input; the builder reports
  non-manifold edge counts in its echo, so the contract is presumably "refuse
  cleanly". Nobody has checked.
- **`case_coincident_faces`** — two cubes unioned along a shared face, so the
  merged mesh has coplanar triangles and no crease where the seam was.

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

## Harness gaps these keep running into

Not cases, but the reason several cases above have to be read by eye:

- **`sandwich` cannot see an under-fill.** It bounds how far the result may
  *stray* from the model; a bead that is too small strays nowhere. Every
  collapsed-bead and truncated-spine defect so far has passed it. A "fills"
  check — no point of the model within the tool's reach of a selected crease may
  be left unblended — would have caught the near-wall collapse and the needle
  runout on the day they appeared.
- **`drops` is per-node, refusal is per-crease.** A `drop` variant asserts the
  whole tool comes out empty, but the gate refuses one crease at a time, so a
  model with one bad crease and one good one can only be `none` today. That is
  why `case_near_wall_fillet`'s large variant is not a `drop`.

## What not to build

The list in [`junction-cases.md`](junction-cases.md) still holds — unequal radii
per edge, oversize radii as their own cases, and anything claiming a hand-written
reference for a blend that has no closed form.
