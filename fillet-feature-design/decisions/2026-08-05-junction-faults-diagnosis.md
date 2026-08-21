# Diagnosis: `rib_into_boss` and `refused_neighbour` junction defects

Diagnosis only. No file under `src/` or `fillet-bench/` was edited. Everything below was
measured; where I could not establish something it says so.

## 0. Instrument provenance — read this before trusting any row

**The binary moved twice under me.** Another agent rebuilt `build/OpenSCAD.app` mid-run.

| binary | md5 | built | what it is |
|---|---|---|---|
| A | `e06b348d…` | Aug 4 19:43:44 | the tree as I found it |
| B | `565b9433…` | Aug 4 23:55:37 | A + the other agent's uncommitted `dropVolumelessParts` rewrite |
| C | `895af325…` | Aug 5 00:10:29 | landed as `0c0727583`; **not measured** |

The first `rib_into_boss` sweep was cut in half by the swap: `$fn`=25 died with rc=138
(SIGBUS) at the exact moment the file was replaced. After that I **copied the whole
`OpenSCAD.app` bundle into scratch** (`binB.app`) and every row below marked *binary B*
came from that frozen copy, which is byte-stable for the whole run. All dylib references
are absolute paths, so the copy is faithful; self-test `rib_into_boss $fn=26` gives the
identical VALID v=441 e=1317 f=878 χ=2 either side.

Binary B carries an **in-progress, uncommitted** change (`Decompose()` → union-find
labelling in `dropVolumelessParts`). It is not a fix for either defect I was asked about
— but it perturbs vertex positions enough to move which parameter values fail. That fact
is itself a finding: see §3.

Models: I used frozen copies in scratch with the radius exposed as `RSET`. Geometry is
identical to `fillet-bench/models/*.scad`. Cross-checked against the in-tree models with
the new `-D R=` axis (commit `624c49daa`): `refused_neighbour` r=0.5 → VALID v=356 e=1062
f=708 χ=2, byte-for-byte the same numbers as my copy. Feature-fired evidence is reported
with every model: `refused_neighbour` warns twice and drops 5 of 24 selected creases;
`rib_into_boss` echoes "selection brush takes 26–27 of 32–33 candidate edge(s)".

**Instrument #11, found here: `mesh.py` on an OFF export is measuring the exporter's
print precision as much as the geometry.** `src/io/export_off.cc:58` streams
`v[i][0] << " " << …` with the default `ostream` precision — **6 significant figures**.
Two vertices 1e-6 apart at coordinate 7.2 print identically, weld into one, and turn a
degenerate-but-combinatorially-manifold solid into a 4-face edge. `--tol 1e-9` does not
help: the collapse already happened in the file. ASCII STL is the sound instrument —
`export_stl.cc:70` uses `double_conversion::ToShortest`, i.e. exact double round-trip.
Every "validity" verdict below is stated for the export it was taken from. Both my
scripts were run on known-good controls first (`cube`, `cylinder $fn=32`, a y-symmetric
box): 0 non-manifold, χ=2, 0 degeneracies.

---

## 1. Both defects reproduce, and the recorded numbers are partly wrong

### `rib_into_boss` — recorded "invalid at `$fn` = 14 and 32"

Reproduced, but the recorded set is **one sample of a random process**, not a property of
the tessellation.

- Binary A, OFF: `$fn`=14 **VALID**, `$fn`=32 INVALID (χ=3, nonman=2).
- Binary B, OFF: `$fn`=14 INVALID (χ=4, nonman=3), `$fn`=32 INVALID (χ=4, nonman=4),
  and additionally **11** and **25**, which the record does not list.
- Binary B, exact ASCII STL: `$fn`=11 and 14 INVALID; **25 and 32 come out VALID**.

And then:

**`rib_into_boss` is nondeterministic run to run.** Eight identical invocations of the
same command, exact ASCII STL, md5 of the export:

| `$fn` | distinct outputs in 8 runs | invalid runs |
|---|---|---|
| 11 | 3 | 8 / 8 |
| 14 | 2 | 7 / 8 |
| 25 | 3 | 0 / 8 |
| 26 (the bench's own value) | 2 | 0 / 8 |
| 32 | 3 | 1 / 8 |

Triangle counts differ between runs (1120 / 1134 / 1138 at `$fn`=32), so this is not the
vertex-order noise the record already knows about — the meshes are genuinely different.
`refused_neighbour` by contrast is **fully deterministic**: 6/6 identical md5 at r = 0.5,
0.8 and 0.9.

So "invalid at `$fn`=14 and 32" should be read as "invalid on the run that was taken".

### `refused_neighbour` — recorded "non-manifold at r = 0.2, 0.8, 0.9, 1.0"

Reproduced on binary A: r = 0.20, 0.35, 0.80, 0.85, 0.90, 0.95, 1.00, 1.05, 1.90 invalid
(OFF, weld 1e-6); r=0.35, 1.05 and 1.90 are additions to the record, r=0.3–0.7 / 1.1–1.8 /
2.0 valid as recorded.

The r=0.9 sliver is confirmed: one edge on 4 faces at
**(0.440817, 3.10247, 7.5) – (0.440817, 3.10213, 7.5), length 3.40e-4**, stable across
weld 1e-4 … 1e-9. The record's "0.34 µm sliver carried by 4 faces" is exactly right.

On binary B (exact STL) the failing set is different: **r = 0.05, 0.10, 0.80, 0.85** in
0.05 steps over [0.05, 2.00], everything else valid.

---

## 2. Where the fault is, geometrically

### `refused_neighbour`: the crossing of two beads' tangency lines

Every failing radius puts the bad edge at **x = 0.440817 exactly, z = 7.5 exactly**, and
at y = ±(4 − r) + δ with δ a few thousandths:

| r | bad edge y | 4 − r | δ | edge length |
|---|---|---|---|---|
| 0.20 | +3.800550 | 3.80 | 5.5e-4 | 7.0e-5 |
| 0.35 | +3.650960 | 3.65 | 9.6e-4 | 1.3e-4 |
| 0.80 | −3.209710 | 3.20 | 9.7e-3 | 4.05e-3 |
| 0.85 | −3.160310 | 3.15 | 1.03e-2 | 4.3e-3 |
| 0.90 | +3.102470 | 3.10 | 2.5e-3 | 3.4e-4 |
| 0.95 | both ±3.05 | 3.05 | 2.3e-3 / 1.15e-2 | 3.6e-4 / 4.8e-3 |
| 1.00 | +3.002740 | 3.00 | 2.7e-3 | 3.7e-4 |
| 1.90 | −2.113970 | 2.10 | 1.4e-2 | 2.0e-5 |

Reading those three coordinates:

- **z = 7.5** is the cube's top face.
- **x = 0.440817** is the line where the tilted box's *bottom* plane cuts z = 7.5. Solving
  the plane by hand gives x = 0.4410 — that is the spine of the oblique concave crease
  (interior angle 80°).
- **y = 4 − r** is the tangency line, on the cube top, of the bead on the *perpendicular*
  concave crease (cube top × box side y = ±4, interior angle 90°, tangency offset
  r/tan45° = r).

So the fault sits where **one bead's spine crosses the neighbouring bead's tangency
line** — the point where both bead surfaces are tangent to the same wall plane (z = 7.5)
and therefore to each other, at zero angle. δ is a few multiples of `eps` (= 1e-3·r), the
overshoot the builder pushes each cell past its wall.

The four faces on the bad edge at r=0.9 are: a triangle in the top plane of area 1.55e-7,
a triangle in the box-bottom plane, and two coplanar triangles ~10° along the bead arc of
areas 1.53e-7 and 1.06e-4. A **sliver wedge**, not a membrane.

This is **not** the refusal. The refusals are all at x = 7.5 (`[7.5, 4, 7.44]` and four
at `[7.5, ±4, 7.26…7.5]`); the fault is at x = 0.44, the other end of the junction. The
record's "the oblique junction, not the refusal, despite the model's name" is confirmed.

I could not settle one thing about that vertex: the junction at (0.4409, ±4, 7.5) is where
the box's own bottom/side **convex** edge pierces the cube top, so a crease the concave
pass does not build leaves it. Whether that makes `seamVertex()` true there — sending it
down the seam-overrun path instead of the corner-cell path — I could not determine without
a probe I am not able to build. It matters for which construction to fix and it is the
single biggest gap in this report.

### `rib_into_boss`: a zero-thickness membrane in a bead's own cell-seam plane

The bad edges are long — 0.05 to 1.22 mm — and they carry **exact duplicate triangles
with opposite orientation**: at `$fn`=26 r=2.25, triangles `t653` and `t654` have the same
three vertices and normals `(+0.4696,+0.8829,0)` / `(−0.4696,−0.8829,0)`. That is a
zero-thickness membrane hanging off the surface — the curved-arrival **fin** of §4, smaller,
exactly as the record guessed.

Localisation:

- Position (8.618, −3.417, 5.923) → cylindrical radius 9.27, z between the plate top
  (5) and the base bead's tangency (7). On the boss's base-arc bead, near the rib.
- The membrane's plane normal is horizontal, at 61.99° in the xy-plane. At `$fn`=26 the
  facet angle is 13.846° and the station tangents lie at k·13.846° + 6.923°; k=4 gives
  **62.31°**. The membrane therefore lies in a **section plane of the boss base-arc
  chain** — the plane where two consecutive cells of that bead abut, which
  `appendChainCells` cuts and `appendSeamCovers` (`FilletBuilder.cc:3304-3313`) exists to
  cover. It is the station where the rib's own base bead crosses.
- The same membrane appears at every failing case I located: `$fn`=11, 14, 25, 32 at r=2,
  and r=1.5 and 2.25 at `$fn`=26.

**The membrane is inside the tool solid, before the caller's union.** Exporting
`fillet_tool(r){part(); brush();}` on its own at `$fn`=26 r=2.25 gives the four
non-manifold edges at *identical coordinates* (8.618225,−3.416656,5.923482) etc. So it is
produced by `buildRoundSolid`'s own cell union / global subtraction, not by
`union(){ part(); fillet_tool(…); }`.

A causal test I ran and must report as **inconclusive**: removing the brush does not
remove the seam vertices, because the size gate then refuses the same vertical creases
itself (`radius 2 does not fit the crease at [7.459, -2.729, 13]`). The brushed and
unbrushed exports are **byte-identical** at `$fn`=26 — trap #6, inertness, caught by
checking rather than assumed.

### What the source already says about both

`cornerProfile`'s comment (`FilletBuilder.cc:2686-2696`) describes this exact failure in
the past tense: *"Two solids whose boundaries touch along the five edges of a shared face
and then leave each other at a fraction of a degree are what a union cannot resolve: on a
spike … it left nine triangles of no area at the one height each bead was cut back at."*
The seam-overrun comment at `:3070-3083` names the other half: *"a touch is what the
caller's union resolves into a flap of zero thickness."* Both remaining defects are that
same failure surviving in places the existing remedies do not reach. The whole epsilon
ladder — `eps = 1e-3·r`, `over = 1.5·eps` (`:3353`), `ballPast = ballShort + 2·eps`
(`:3354`), `seamOver·r = 0.10·r` (`:3135`) — exists for it.

---

## 3. One mechanism or two

**One root cause, two distinct surface geometries.** The evidence, both ways.

For one:

1. Both are at a **bead–bead crossing** inside a junction, not on a plain bead.
2. Both are invisible to Manifold. In *every* failing case OpenSCAD reports
   `Status: NoError, Genus: 0`, and its vertex count exceeds the exact-STL welded count by
   exactly the number of coincident pairs (359 vs 358 at r=0.8; 562 vs 560 at `$fn`=32;
   530 vs 528 at r=0.10). The solid is combinatorially manifold and geometrically
   degenerate — two distinct vertices at bit-identical positions, i.e. a pinch. An odd χ
   with `nonman=0` (r = 0.05, 0.10) is the signature of exactly one such pinch.
3. Both are unstable rather than parametric — see §4. Neither depends on the parameter
   value in any structured way, and an unrelated code change (binary A → B) reshuffled
   both failing sets wholesale.
4. Both are the failure the source comments already name, and both live in the same
   epsilon ladder.

For two:

1. **Scale and character differ.** `refused_neighbour` produces a sub-micron *sliver
   wedge* — two nearly coplanar triangles of area ~1.5e-7 at the tangency line, no
   duplicate triangle anywhere. `rib_into_boss` produces a *zero-thickness membrane* with
   exactly duplicated back-to-back triangles of area 8e-4 … 2.2e-2 and edges up to 1.2 mm.
   A vertex-welding cleanup would fix the first and not touch the second.
2. **Different coincident plane.** The sliver lies against the *wall* plane the two beads
   are both tangent to. The membrane lies in a *section plane of a chain's own cells*.
3. **Determinism differs.** `rib_into_boss` is nondeterministic run-to-run;
   `refused_neighbour` is not, at any radius I tested.

My reading: the shared cause is the design decision to separate coincident surfaces by a
small offset and let the boolean resolve the crossing. That works generically and fails at
the isolated points where the crossing is *tangential by construction* — which is what a
bead–bead junction is, since both beads are tangent to the wall they share. The two
observed remnants are the two ways a boolean can express a tangential contact: a sliver
where surfaces graze, a membrane where they coincide over a region. **A single fix at the
"how do we separate surfaces" level would address both; a single fix at the mesh-cleanup
level would address only one.**

---

## 4. The failing sets: scattering, not band structure

All rows binary B, exact ASCII STL, single run.

### `rib_into_boss`, `$fn` swept 8 … 64 (all 57 values)

Invalid: **11, 14**. Valid: everything else, including 25 and 32.
On the same binary read through the 6-s.f. OFF instead: invalid at **11, 14, 25, 32**.
On binary A through OFF: invalid at **32** only (of 8…26 measured before the swap).

A leading indicator makes the instability visible far more widely than the invalid rows
do. The model is exactly mirror-symmetric in y, and the bare union exports perfectly
y-symmetric (0 unmatched vertices), as does `rib_into_boss` at `$fn`=26 (0 unmatched,
worst matched pair 2.3e-13). But **`$fn` = 8, 10, 11, 12, 13, 14, 15, 16, 25, 28, 31, 32,
37 all export y-asymmetric meshes**, and at `$fn`=14 three of the eight unmatched vertices
are precisely the endpoints of the non-manifold edges. The instability is present at ~40 %
of tessellations; it only reaches non-manifoldness at a few.

### `rib_into_boss`, radius swept at `$fn`=26

0.5, 0.75, 1.0, 1.25, 1.75, 2.0, 2.5, 2.75, 3.0 valid; **1.5 and 2.25 invalid**.
Same scattering in the other axis.

### `refused_neighbour`, radius swept 0.05 … 2.00 in 0.05 steps

Invalid: **0.05, 0.10** (χ odd, a pinch, and a different regime — only one warning fires,
so fewer creases are refused and many more are built), and **0.80, 0.85**. All 36 other
values valid.

Refined to 0.01 steps over [0.76, 0.89]: invalid only at **0.80** and **0.85** — isolated
points, not an interval.

Refined again around 0.8:

| r | verdict |
|---|---|
| 0.7960, 0.7980, 0.7990, 0.7995 | VALID |
| **0.7999** | INVALID |
| 0.79999 | VALID |
| **0.8** | INVALID |
| 0.80001, 0.8001, 0.8005, 0.801 | VALID |
| **0.802** | INVALID |
| 0.804 | VALID |

**Answer: a scattering, with no band structure at any resolution I probed.** Valid at
0.79999 and 0.80001 and invalid at 0.8 is not a geometric regime; it is a floating-point
coincidence. Overall hit rate across everything I ran: roughly 5–15 % of parameter values,
in either axis, on either model.

---

## 5. Proposed fix

### What the diagnosis rules out

No change to parameter handling, no threshold, and no change to which creases are selected
can fix either defect. The failing sets are not parameter-shaped. Equally, **the record's
framing of both defects as "invalid at these values" should be retired** — both are "fails
at ~10 % of values, and which 10 % moves when anything else changes".

### The fix, in two parts

**Part 1 — remove the degenerate remnants from the finished tool solid. Fixes the
`refused_neighbour` family.**

The remnants are sub-micron: coincident vertex pairs and triangles of area 1e-7 mm² on a
model of size 15 mm. `Manifold::Simplify(double tolerance)` and
`Manifold::SetTolerance(double)` both exist in the vendored library
(`submodules/manifold/include/manifold/manifold.h:214-215`) and are not called anywhere in
`FilletBuilder.cc`. Applying `Simplify` to the tool before it is returned, with a tolerance
tied to the radius and set **well below the epsilon ladder** — 1e-6·r is three orders under
`eps = 1e-3·r`, so nothing the construction deliberately builds is within reach — collapses
exactly the features that have no geometric meaning.

Why that is the right shape: the builder's whole strategy is to offset surfaces by a known
small amount and let the boolean sort out the crossing. That strategy has a stated tolerance
already (`eps`), so declaring the same tolerance to Manifold makes the two consistent
instead of leaving Manifold to preserve detail three orders finer than anything the builder
means. It is also the only part of this that is provably safe: a feature below the tolerance
the construction itself works at cannot be load-bearing.

**Part 2 — the membrane. Fixes the `rib_into_boss` family. Two options, and I am not sure
which is right.**

The membrane is 0.05–1.2 mm across and zero thick. `Simplify` will not remove it — it is
not small. `dropVolumelessParts` will not remove it either, in the old form or in the
rewrite landing now, because it is **attached to the main component**, not a separate one.
Nothing in the pipeline currently looks for it.

- (a) *Delete it.* A triangle appearing twice with identical vertices and opposite
  orientation bounds no volume and is always removable; deleting the pair and re-stitching
  the surrounding fan is a local, volume-preserving operation. Robust and general — it
  catches whatever produces a membrane, including the curved-arrival fin family. But
  re-stitching a fan on a mesh Manifold owns is real work, and I have not established that
  every membrane here is a clean duplicate pair (I confirmed duplicate pairs at
  `$fn`=26 r=1.5 and 2.25 and `$fn`=32 r=2; the `$fn`=11 and 25 faults show no duplicate
  triangle and may be something else).
- (b) *Stop producing it.* The membrane lies in a chain section plane where two consecutive
  cells abut and `appendSeamCovers` was supposed to cover the join. The established remedy
  in this file is to guarantee an **angle** rather than an offset (`cornerProfile`'s `over`,
  `:2732-2735`). Making consecutive cells of a chain overlap along the spine rather than
  abut on a shared plane would do the same for this join. Cleaner in principle, but it
  touches the construction every bench model depends on, and I cannot predict its blast
  radius from outside a build.

I would do Part 1 first — it is small, testable and self-contained — and (a) before (b).

### What would confirm it

1. **Run the two sweeps again.** `refused_neighbour` r = 0.05 … 2.00 at 0.01 and the
   ultra-fine grid round 0.8 (0.7999, 0.8, 0.802 are the three known reds); `rib_into_boss`
   `$fn` 8 … 64 and r 0.5 … 3.0. All green, on **exact ASCII STL**, not OFF.
2. **The y-symmetry probe as the leading indicator**: `rib_into_boss` must export a
   y-mirror-symmetric mesh at every `$fn`, as it already does at 26. Today it fails at 13
   of the 33 tessellations I checked. This is much more sensitive than the validity check
   and it is self-proving — the model *is* symmetric, so any asymmetry is the builder's.
3. **Determinism**: eight runs of the same command must give one md5, at `$fn` = 11, 14, 25,
   26, 32. Today they give 2–3.
4. **Feature-fired guard**, per the standing rule: `refused_neighbour` must still warn twice
   and drop 5 of 24 creases; `rib_into_boss` must still echo "brush takes 26 of 32". If a
   fix makes either number move, it changed what is built, not how it is cleaned up.
5. **No regression on the corpus.** Part 1 in particular must be shown inert on solids that
   were already valid — a byte-diff is the wrong test (17 of 225 are nondeterministic) but
   volume and genus per model are not.

### What I am not sure about

- **Whether the two defects share a construction.** They share a root cause and a
  neighbourhood; I could not prove they share a code path, and the evidence in §3 cuts both
  ways. If forced: the same fix at the surface-separation level would catch both, but the
  cheap fix (Part 1) will only catch one.
- **Whether `refused_neighbour`'s failing vertex is a seam vertex or a corner-cell
  junction.** This decides which construction Part 2 would have to touch for that model.
  Settling it needs one probe printing `seamVertex(j.vert)` at (0.4409, ±4, 7.5) — a
  rebuild I was not able to make.
- **What the `$fn` = 11 and 25 `rib_into_boss` faults are.** They have no duplicate
  triangle, so they may not be membranes at all. I located them (near (8.08, 3.48, 5.69)
  and (7.14, 3.66, 6.26), same region) but did not characterise them.
- **Whether `Simplify` is safe at high `$fn`.** At `$fn`=192 the blend arcs' own facets get
  small; 1e-6·r should still be far below them, but I have not measured it.
- **Whether the nondeterminism has a separate cause.** It is probably parallel reduction
  inside Manifold, and it may be the mechanism that *randomises* which values fail rather
  than a second bug. `refused_neighbour` is deterministic and still fails, so
  nondeterminism cannot be the whole story.
- **Everything measured on binary B carries an uncommitted third-party change.** The
  qualitative findings (scattering, degeneracy-not-topology, membrane in the tool, location)
  held on both binaries A and B, so I believe them; the exact failing *sets* did not, and
  should be re-measured on whatever finally lands.

### One thing to fix in the instrument regardless

`fillet-bench` reads validity from a 6-significant-figure OFF. That gave a **false red** at
`rib_into_boss $fn`=25 and `$fn`=32 and at `refused_neighbour` r=0.9, 0.95, 1.0, 1.05 — the
exact STL calls all of those valid on the same binary. A1 should be taken from ASCII STL, or
`export_off.cc` should be given a precision. Without that, part of the recorded failing sets
is the exporter and not the operator.
