# The inner curved corner, rendered — and the seated-bead fallback judged

Branch `worktree-agent-a1065db1b78cc5fd6`. Companion to
`2026-08-04-arrives-straight.md`, which has the full A/B measurement.

The owner asked to *see* the curved-arrival corner, and stated a hypothesis:
**"putting a bead in was also broken"** — i.e. that the seated-ball fallback
`arrivesStraight` diverts to is itself bad geometry. This record tests that.

**Verdict: the hypothesis is correct.** At a curved arrival the fallback does
not produce a bead. It produces a pair of zero-thickness sliver fins meeting at
a knife edge, and the resulting mesh is **not a valid solid**.

---

## Model — `bcurve.scad`, run UNMODIFIED

Source: `git show fix-blockers:work/rev2/bcurve.scad`. It is a recorded model
and was run exactly as recorded, with no edits.

A plate, a cylindrical boss (R=8, `$fn`=64), and a tall rib running out of the
boss. A slab brush at plate level leaves the rib/boss vertical creases
unfilleted, so the two vertices where the rib meets the boss at plate level are
genuine brushed **seam vertices**, and the crease arriving at them is the
**tessellated base arc of the cylinder** — a curve. This is precisely the case
`arrivesStraight` exists to detect.

**Crease threshold confirmed off its default.** The model sets
`$fn=64; $fa=360/FN; $fs=0.01;`, so the threshold is
`1.5 × max($fa, 360/$fn) = 1.5 × 5.625 = 8.44°`, **not** the 18° that a
`$fn`-only model would be measured through.

**Warnings: none.** Neither arm emitted a single "does not fit" warning, and
both reported `Status: NoError`. So no radius was refused and skipped — the
fillet in these pictures actually happened. This was checked rather than
assumed, because a skipped radius still exports a plausible mesh.

## Binaries — the same two the measurement was taken with

Rebuilt after the host wiped the scratchpad, and verified byte-identical to the
originals:

| arm | size | md5 | `arrivesStraight` symbol (`nm -a`) |
|---|---|---|---|
| `kept` (HEAD, fallback taken) | 24289736 | `e86ba8aaf4bf…` | **1** |
| `removed` (gate deleted, beads run past) | 24289576 | `9663bb9defff…` | **0** |

Both md5s match the values recorded in the measurement document exactly, so
these are the same binaries, not lookalikes. The symbol is present in one and
absent from the other, which is direct evidence the deletion compiled in.

**The arms differ on this model** — exported OFFs have different md5s, different
triangle counts (3496 vs 3020) and different volumes. Checked *before* rendering,
because the predicate is inert on `rib.scad` and would have produced two
identical pictures there.

## Instruments, validated on known answers

* **`meshnum.py`** — genus from the Euler characteristic `V−E+F = 2(C−G)` of the
  welded triangle mesh, **derived from the mesh, never from ECHO**. Validated on
  two cases whose answers are known before use: a plain cube reads
  `genus=0 comps=1 nonman=0 boundary=0 vol=1000.000000` exactly, and a block
  with a through-hole reads `genus=1`.
* **`wire.py`** — a pure-stdlib shaded+wireframe rasteriser (zlib+struct only;
  no matplotlib/PIL available). It renders the **actual exported OFF**, so the
  pictures and the numbers are of the same geometry. Validated by reproducing
  OpenSCAD's own render of the same OFF at the same camera.
* Non-manifold counts are tolerance-sensitive, so they are read **for direction
  and zero only** and taken at two welds.

---

## The numbers beside the picture

Weld **1e-7 absolute** (the D17 review's own tolerance):

| | `kept` — fallback taken | `removed` — beads run past |
|---|---|---|
| components | 1 | 1 |
| **mesh genus** | **−1.5** | **0** |
| Euler χ | 5 | 2 |
| **non-manifold edges** (>2 faces) | **5** | **0** |
| boundary edges (=1 face) | 0 | 0 |
| triangles | 3496 | 3020 |
| volume | 21748.293961 | 21749.311205 |
| "does not fit" warnings | 0 | 0 |

Identical at weld **1e-6**, so the 5-vs-0 is not a tolerance artefact.

### `genus = −1.5` is the headline

A closed orientable surface cannot have a fractional genus. A non-integer genus
is only possible because χ is **odd** (5), which a closed 2-manifold's Euler
characteristic can never be. **This is a proof, not an impression, that the
`kept` mesh is not a valid closed solid.** The `removed` mesh is χ=2, genus 0,
zero non-manifold edges, zero boundary edges — a textbook valid single solid.

---

## The images

All under `fillet-feature-design/arrives-straight-images/`. Every pair uses the
**same camera** for both arms so they can be flipped between. Camera is the
explicit eye/centre vector form (`--camera=eyex,eyey,eyez,cx,cy,cz`); the gimbal
form put the eye inside the rib.

The corner under inspection is the seam vertex at **(7.4130, 3, 5)** — where the
rib's base crease meets the boss's base arc at plate level.

| file | what it is | camera |
|---|---|---|
| `bcurve-context-kept.png` / `-removed.png` | wide shot locating the corner on the part | eye `34,30,26` → centre `4,0,6`, OpenSCAD `--render` |
| `bcurve-corner-shaded-kept.png` / `-removed.png` | the corner as the eye reads it | eye `15.5,17,12.6` → centre `7.4,3,5`, OpenSCAD `--render` |
| `bcurve-corner-mesh-kept.png` / `-removed.png` | same view with every mesh triangle drawn | same camera, `wire.py`, fov 30 |
| `bcurve-corner-zoom-kept.png` / `-removed.png` | tight zoom on the defect itself | same eye/centre, `wire.py`, fov 13 |

### What is visible

* **`bcurve-context-kept.png`** — the bead runs cleanly around the boss and
  along the rib, and then at the rib/boss junction there is a thin upright
  **fin** standing out of the plate. It is small on the part, which is why the
  close-ups exist.
* **`bcurve-context-removed.png`** — same view, the blend runs continuously
  around the boss and into the rib. No fin.
* **`bcurve-corner-shaded-kept.png`** — the fin fills the frame: two curved
  sail-shaped surfaces rising from the plate and meeting at a sharp vertical
  cusp. It looks like a tent, not a fillet. Nothing about it is a bead.
* **`bcurve-corner-shaded-removed.png`** — a smooth continuous fillet turning
  the corner. The blend simply flows from the rib's crease into the boss's arc.
* **`bcurve-corner-mesh-kept.png`** — with triangles drawn, the fin is a fan of
  long thin slivers converging on a single apex, standing proud of both the
  plate and the boss wall.
* **`bcurve-corner-mesh-removed.png`** — even, well-proportioned triangles
  throughout; the two beads meet along one clean seam line.
* **`bcurve-corner-zoom-kept.png`** — the two bead end-faces have been cut back
  to a razor-thin tent and meet along a knife edge. **This edge is the
  non-manifold one**: surfaces from both beads and the fin all meet there, which
  is what the 5 `>2`-face edges and the odd Euler characteristic are counting.
* **`bcurve-corner-zoom-removed.png`** — the same region: a plain crease where
  the two beads overlap and meet. All triangles well-formed, no fins, no cusp.

---

## Verdict on the seated-bead fallback

**It is broken, and the failure mode is: degenerate sliver fins with a
non-manifold knife edge.** Characterised precisely:

* **Not open** — 0 boundary edges. It is not a hole.
* **Not disconnected** — 1 component. It is not shattered into pieces.
* **Non-manifold and slivered** — 5 edges carried by more than two faces, χ odd,
  genus non-integer. The two bead ends are truncated back to near-zero-thickness
  sails which meet along a shared edge instead of closing over a corner ball.
* **Visible to the eye** — it is a fin standing proud of the plate, not a
  sub-pixel artefact.

So the mechanism `arrivesStraight` protects *by handing the corner back to* is
itself producing invalid geometry at exactly the corners it is handed. The
predicate's stated rationale — "a seated ball has no such problem" — **does not
hold on this model.** The seated ball is not being built here at all; what the
fallback actually yields is a truncated pair of beads with nothing filling the
corner between them.

This does not by itself decide `arrivesStraight`'s fate — the companion
measurement found removing it makes three corpus models worse and none better,
and the review's `0/37` required a second change (global subtraction) that was
shipped in the opposite direction. But it does remove one of the two pillars the
predicate stood on: **the fallback it diverts to is not the safe construction it
was documented to be.**

---

## What was NOT rendered

* **Only `bcurve.scad`.** `mixc` and `mixp` (the mixed served/withheld models,
  where removal *regresses*) were not rendered — so the pictures show the case
  where removal helps and **not** the case where it hurts. That is a real gap in
  the visual story and the numbers for it are in the companion record.
* **No section cut.** The fin is visible from outside, so a cut was not needed,
  but a cut would show whether the fin has any interior thickness at all.
* **No render of the corpus regressions** (`box_step_*_r3`), which are the
  models that argue *for* keeping the predicate. Nobody has looked at those by
  eye; they are the natural next images.
* **No `$fn` sweep of the pictures** — everything is at the model's own `$fn`=64.
  Whether the fin shrinks or persists under refinement is measured in the
  companion record (it persists: `bcurve` is bad at 13 of 37 tessellations on
  `kept`) but is not shown here.
* **No highlighting of the 5 non-manifold edges in-image.** Their location is
  inferred from the zoom, not marked; a renderer pass colouring `>2`-face edges
  would prove the identification rather than argue it.
* **No CGAL-backend render.** Manifold backend only.
