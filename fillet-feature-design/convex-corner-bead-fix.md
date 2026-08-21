# Convex corner beads — diagnosis and fix

Addresses defect ① of `corner-defect-taxonomy.md` ("convex box corners — crude
single-apex `cornerBall` fan"), which the `emitCap` rewrite improved but did not
remove. Visible on every right-angle convex corner in the bench: `cube_control`
(S3-T08), `box_step` (S3-T02), `lbracket` (S3-T01), `chamfer_box`, and the plate
corners on `boss_plate` / `hole_plate` / `two_bosses` / `rib_into_boss`.

Companion diff: `convex-corner-bead-fix.patch` (the `FilletBlend.cc` change, also
committed on this branch). **Not built or bench-rendered yet** — verified only with
a standalone geometry harness; see "Verification" below.

---

## Symptom

At a convex trihedral corner the three round-over strips do not flow into a clean
rounded corner. Instead:

- a small **bead pinched at the centre**, with
- an **uneven ring around it** — dark, recessed-looking wedges where the three arcs
  meet, so the corner reads as a beaded knuckle rather than a continuation of the
  strips.

The eye reads it as if the corner sphere were too small, or sitting at the wrong
height relative to the corner.

## Root cause — tessellation, not geometry

The geometry is correct. For a right-angle corner:

- `setback = size · tan(45°) = size`, so each strip's tangent inset sits one radius
  in from the edge;
- `cornerBall(u, convex)` solves for the point at distance `size` from all three
  faces — the same point each incident edge's rolling ball centres on;
- therefore **every boundary-ring vertex lies exactly on the sphere of radius
  `size` about that centre** (harness: all rim radii = the sphere radius to 1e-12),
  and each round-over strip (a quarter-cylinder of radius `size`) is tangent to that
  sphere along its truncation circle. A rounded cube corner is a sphere meeting three
  cylinders smoothly. There is no real step and no wrong-size bead.

What beads is `emitCap`'s **topology**. It tessellates the cap as a pole-and-rings
fan: every boundary point is slerped inward to **one shared apex** (`emitCap`,
`FilletBlend.cc`). That apex is the "perfect bead in the middle"; the first slerp band
between the rim and the apex is the "imperfect ring". Because all facets swing toward
the single pole, they do not run parallel to the three strips, so:

- the seam angle between cap and strip is **uneven** along each arc — near-smooth
  mid-arc (~0.3°) but kinking ~11–12° right at the three arc joins (harness
  measurement), which is where the dark recessed wedges appear;
- the pole is a high-valence pinch in the middle.

An octant of a sphere has three-fold symmetry; a pole fan imposes an arbitrary
n-fold symmetry around one point, which is the mismatch you see.

## Fix

Replace the fan, **for genuine convex corners only**, with a barycentric
subdivision of the spherical triangle bounded by the three fillet arcs
(`emitCapTri`):

- three corner directions `u0,u1,u2` from the sphere centre to the three sector
  corners; interior grid point `(i,j)` is the barycentric mix
  `(seg-i-j, i, j)/seg` renormalised onto the sphere;
- **no central pole**; grid lines run parallel to the three incident strips, so the
  cap shades as an even continuation of them;
- the three boundary rows **reuse the exact ring vertices**, so the cap welds to the
  strips even when the arcs are not perfect great circles;
- the cap seats its interior on the **rim's own mean radius**, not a hard `size`.
  Identical to `size` at a right-angle corner; at an oblique corner — where the rim
  is not perfectly on the `cornerBall` sphere — this removes the annular step the
  hard radius would leave.

`emitCorner` now tags which ring points are sector-inset corners (`cornerPos`) and
calls `emitCapTri` only when the ring is exactly **three equal-length arcs and all
edges are convex**. Concave caps (pockets, defect ③) and any other ring fall through
to `emitCap` unchanged, so the blast radius is convex box/plate corners alone. Any
mismatch → `emitCapTri` returns false → current behaviour.

## Not touched (possible follow-ups)

- **Concave caps** (defect ③, pocket apex spike) — still `emitCap`.
- **Oblique-corner arc clamp** (`crossSectionEdge`, `FilletBlend.cc:753`) — pulls
  convex arc interiors onto the solid side of each face plane and can add a real
  concave dip at asymmetric corners. Does not fire on the symmetric cube; the next
  suspect if a dip persists on non-90° models after this change.
- **Mixed-sign saddle** (defect ②) — unrelated; unchanged.

## Verification

Done here (no toolchain in the working environment):

- symbolic + numeric harness confirming the rim lies on the sphere for a 90° corner,
  and that `emitCapTri`'s subdivision welds to three round-over strips pole-free;
- per-seam dihedral measurement: pole-fan seam smoothness is uneven (0.3°–11.6°,
  kinks at the arc joins); the subdivision is uniform.

Still needed (on a build):

- `./sheet.sh S3-T08` (cube_control) and `S3-T02` (box_step) — corners should shade
  as an even continuation of the strips, no central bead;
- a full `./sheet.sh` to confirm no VALID tile regresses (mesh stays manifold,
  `warnings=0`), since `emitCapTri` adds interior vertices and leans on `orient()`
  for winding exactly as `emitCap` does.
