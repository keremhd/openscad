# Fillet operator — M1 visual test harness

Hand-written, **operator-independent** reference cases for the fillet/round/
chamfer work (see `../fillet-operator-plan.md` and `../detailed-milestones.md`).
Every tool here is built from the classic OpenSCAD idioms (annulus − torus,
wedge − cylinder, …), so these render with a stock OpenSCAD today and define the
known-good target that milestones **M5** (chamfer/bevel) and **M7** (fillet/round)
must reproduce.

## How to read a case

Geometry is arranged so the feature **edge runs along Z** (or the ring axis is Z),
and each file takes a **thin-slab cross-section** so one flat image is fully
legible:

- **Columns** along X: `[ base model ] [ applied result ] [ isolated tool @ +100 ]`
- **Rows / groups**: a **small** radius and a **large** radius (the large one
  deliberately ≥ the available face, to show the §6.5 overflow/clamp case).
- **Junctions** use a **contour stack**: several sections at rising Z laid out
  along Y, so the section *evolving toward the meeting point* is visible at once.

`_fillet_ref.scad` holds the shared reference tools and the section/stack viewers.

## Cases

| file | feature | sign / tool | slice |
|---|---|---|---|
| `case_inner_corner.scad`   | inner 90° edge (L of two cubes) | fillet + chamfer (union) | horizontal |
| `case_hole_mouth.scad`     | through-hole mouth (convex ring) | round (subtract) | vertical, through axis |
| `case_boss_fillet.scad`    | boss base (concave ring) | fillet (union) | vertical, through axis |
| `case_outer_edge.scad`     | cube outer edge (convex) | round (subtract) | horizontal |
| `case_junction3.scad`      | 3-face apex / pocket (low-`$fn` cone) | round + fillet | contour stack |
| `case_junction4.scad`      | 4-face apex / pocket | round + fillet | contour stack |
| `case_brush_halfchain.scad`| partial edge selection | fillet, flat cap | contour stack |

The two junction files have **no hand-written blend** on purpose — the correct
trihedral/valence-4 corner (plan §6.3 / §6.3.1) has no simple closed form and is
precisely what M8/M9 build. They exist to visualise the section target so the
operator's future output can be dropped into the same stack.

## Rendering

Use the bundled script — it renders every `case_*.scad` into `png/` and picks the
right camera (top-down, or front for the through-axis ring cases):

```
./render.sh                                   # all cases -> ./png/
./render.sh case_inner_corner.scad            # just one
OPENSCAD=/path/to/openscad ./render.sh        # override the binary
SIZE=1600,1600 ./render.sh                     # bigger images
```

It looks for a local build (`../build/OpenSCAD.app/...` or `../build/openscad`)
and falls back to `openscad` on `PATH`. Equivalent one-liner for a single file:

```
openscad -o png/case_inner_corner.png --imgsize=1000,1000 --projection=o \
         --camera=0,0,0,0,0,0,600 --viewall --autocenter --render=1 \
         case_inner_corner.scad
```

(`--render=1` forces the CGAL/Manifold section geometry rather than preview.)

## Permanent location (proposal)

For now these live here, outside the build. When they graduate to automatic
regression tests (milestone **M6**), the natural home is
`tests/data/scad/3D/features/` — CTest auto-globs `*.scad` there (non-recursive)
into the dump/render/preview/throwntogether suites, with committed baseline PNGs
under `tests/regression/<suite>/`. Files that reference `fillet_tool` etc. can
only move there once M5/M7 land; the operator-free references could move sooner.
