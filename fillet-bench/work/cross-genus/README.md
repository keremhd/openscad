# cross.scad — where the genus comes from, and the detached fragment at r = 0.9

Working directory, untracked. Nothing here is committed and no tracked file was modified.

## The binary

**Two builds are involved, and that is worth reading.** The app bundle was copied to a
scratchpad before any render, exactly as intended:

    binary A   md5 11b6b3b1973c46d671a5e8443fb4640f   build/ mtime 2026-08-05 01:08:19

Everything up to 15:01 was rendered from that copy. At 14:51:20 the concurrent agent replaced
`build/OpenSCAD.app/.../OpenSCAD` with

    binary B   md5 fd3dec786abcd1e3d5dc89b3e92d1c06   build/ mtime 2026-08-05 14:51:20

and at 15:01:06 **the scratchpad copy itself was overwritten with binary B by a sibling
process sharing the same session scratchpad directory.** So the pin held for fourteen minutes
and then silently failed — a copy in the shared scratchpad is not a pin. (A private
subdirectory, `<scratchpad>/pin-crossgenus-b/`, was used from then on.) Binary A no longer
exists anywhere on this machine.

Which artefacts came from which:

* **binary A** — all four `full_*.png`, all four `closeup_*.png`, `diag_r2.0_C5.png`,
  `cross_r{0.9,1.5,2.0}.stl` and every genus/χ number read off them, the component analysis
  and split of r = 0.9, the radius bracket 0.6…1.3, the shell/octant/ball handle probes,
  the pinch scan, `frag_r0.9_{context,where}.png`.
* **binary B** — `tunnel_r{1.0,2.0}_section*.png`, `frag_r0.9_isolated.png`, `warnings.txt`.

**The two builds were then cross-checked on this model and agree exactly.** Re-exported under
binary B at weld 1e-6, r = 0.5/1.0 → comp 1, χ 2, genus 0; r = 0.9 and 0.95 → comp 2, χ 4,
genus 0 with a byte-identical 4-face fragment at the same coordinates and the same volume to
six figures; r = 1.5 → χ −2, genus 2; r = 2.0 → χ −6, genus 4 — the same v/e/f as binary A on
every cell. No conclusion here depends on which of the two produced a given file.

All invocations carry `--enable=fillet --backend=Manifold --render`. `-o /dev/null` was never
used.

## Model

`cut.scad` and `frag.scad` in this directory reproduce `fillet-bench/models/cross.scad`
verbatim (three `d=12 h=40` cylinders on the three axes, wrapped in `fillet(r=R)`, `$fn=0`)
and add cut/probe modes. The full-part PNGs at the four radii were rendered from the real
`models/cross.scad` with `-D R=…`; the sections and probes from `cut.scad`, whose `MODE 0`
is the identical solid (verified: same vertex/facet counts and same reported genus).

## Files

| file | what it is |
|---|---|
| `full_r{0.5,1.0,1.5,2.0}.png` | the whole part, one camera, 1000×1000, ortho |
| `contact_full.png` | the four above as a 2×2 sheet |
| `closeup_r{0.5,1.0,1.5,2.0}.png` | same camera, zoomed onto the central junction |
| `contact_closeup.png` | the four above as a 2×2 sheet |
| `tunnel_r2.0_section.png` | **the proof.** 0.15 mm slab at y = −5.5, r = 2.0, viewed along +y. A hole fully enclosed by material |
| `tunnel_r2.0_section_wide.png` | same slab, 3 mm field, so the hole is placed in its pocket |
| `tunnel_r1.0_section.png` | the identical slab at r = 1.0 — no hole. The control |
| `diag_r2.0_C5.png` | half-space cut on the (1,1,−1) body diagonal at 5 mm |
| `frag_r0.9_isolated.png` | **the r = 0.9 second component, alone**, viewed face-on, 0.15 mm field |
| `frag_r0.9_context.png` | the fragment plus the main solid clipped to a 0.25 mm ball about it |
| `frag_r0.9_where.png` | whole part with a 2 mm marker ball at the fragment, showing which pocket |
| `cross_r{0.9,1.5,2.0}.stl` | exact ASCII STL exports the numbers were read from |
| `frag09_0.stl`, `frag09_1.stl` | r = 0.9 split into its two components (0 = main, 1 = fragment) |
| `warnings.txt` | the complete warning text of every radius rendered |
| `cut.scad`, `frag.scad`, `probe.py` | the rig |

## Commands

Pinned binary abbreviated to `$SC`; all renders add `--enable=fillet --backend=Manifold --render`.

    # full part, r ∈ {0.5,1.0,1.5,2.0}
    $SC --enable=fillet --backend=Manifold --render -D R=$R \
        --camera=0,0,0,55,0,25,140 --imgsize=1000,1000 --projection=o \
        --colorscheme=Tomorrow -o full_r$R.png fillet-bench/models/cross.scad

    # junction close-up (same camera, dist 45)
    $SC ... -D R=$R -D MODE=0 --camera=0,0,0,55,0,25,45 --imgsize=1000,1000 \
        --projection=o --colorscheme=Tomorrow -o closeup_r$R.png cut.scad

    # the proof section: 0.15 mm slab normal to y at y = -5.5, looked at along +y
    $SC ... -D R=2.0 -D MODE=9 -D AX=1 -D C=-5.5 -D T=0.15 \
        --camera=4.74,-2.70,-3.66,4.74,-5.70,-3.66 --imgsize=1000,1000 \
        --projection=o --colorscheme=Tomorrow -o tunnel_r2.0_section.png cut.scad
    # ... and the r=1.0 control, same line with -D R=1.0

    # diagonal cutaway
    $SC ... -D R=2.0 -D MODE=8 -D DX=1 -D DY=1 -D DZ=-1 -D C=5 \
        --camera=0,0,0,125.264,0,45,90 --imgsize=800,800 --projection=o \
        --colorscheme=Tomorrow -o diag_r2.0_C5.png cut.scad

    # r = 0.9 fragment, isolated, face-on to its own plane
    $SC ... -D FMODE=0 --camera=4.3255,-4.8958,-3.4212,4.5797,-5.0608,-3.8475 \
        --imgsize=1000,1000 --projection=o --colorscheme=Tomorrow \
        -o frag_r0.9_isolated.png frag.scad

    # exports the mesh numbers were read from
    $SC ... -D R=$R -D MODE=0 -o cross_r$R.stl cut.scad

## Warnings

**Every** radius rendered warns, including the two that measure VALID and genus 0. Full text
in `warnings.txt`; the shape of it is always the same — a large majority of the selected
creases do not fit and are silently dropped:

| r | warning |
|---|---|
| 0.5 | `radius 0.5 does not fit 42 of the 68 crease(s) selected; the worst is at [-4.666, -4.278, -4.091] - another feature 0.534685 away needs the same material` |
| 0.9 | `radius 0.9 does not fit 39 of the 56 crease(s) selected; the worst is at [4.399, 3.995, 4.942] - another feature 0.848807 away needs the same material` |
| 1.0 | `radius 1 does not fit 38 of the 52 crease(s) selected; the worst is at [-4.378, 5.065, 4.018] - the blend would leave the surface it is meant to meet, by 0.934916` |
| 1.5 | `radius 1.5 does not fit 42 of the 58 crease(s) selected; the worst is at [-5.38, 3.776, -4.636] - another feature 1.93024 away needs the same material` |
| 2.0 | `radius 2 does not fit 40 of the 64 crease(s) selected; the worst is at [-3.767, 4.608, -6.045] - another feature 2.36908 away needs the same material` |

Every listed drop point sits at |x|,|y|,|z| ≈ 3.7 … 6, i.e. in the eight body-diagonal corners
where three crease curves converge. That is the same neighbourhood the handles appear in.

## Genus, measured here, on the same binary

`fillet-bench/mesh.py` on the exact ASCII STL exports (never OFF), reported at three weld
tolerances. **Weld tolerance 1e-6 is the headline; 1e-4 and 1e-9 are shown so the reader can
see the answer does not move.** 1e-15 was never used.

    cross_r1.5.stl  VALID v=1352 e=4062 f=2708 comp=1 bnd=0 nonman=0 chi=-2 genus=2  tol=1e-4
    cross_r1.5.stl  VALID v=1354 e=4068 f=2712 comp=1 bnd=0 nonman=0 chi=-2 genus=2  tol=1e-6
    cross_r1.5.stl  VALID v=1354 e=4068 f=2712 comp=1 bnd=0 nonman=0 chi=-2 genus=2  tol=1e-8
    cross_r2.0.stl  VALID v=1528 e=4602 f=3068 comp=1 bnd=0 nonman=0 chi=-6 genus=4  tol=1e-4
    cross_r2.0.stl  VALID v=1528 e=4602 f=3068 comp=1 bnd=0 nonman=0 chi=-6 genus=4  tol=1e-6
    cross_r2.0.stl  VALID v=1528 e=4602 f=3068 comp=1 bnd=0 nonman=0 chi=-6 genus=4  tol=1e-8

Independently confirms the recorded χ = −2 / genus 2 at r = 1.5 and χ = −6 / genus 4 at
r = 2.0, and OpenSCAD's own `Genus:` line agrees on the same renders.

## Where the handles are

Three probes, all on the pinned binary, agree.

1. **Radial shell.** Intersecting the r = 2.0 solid with a centred cube of half-size L:
   L = 4, 5 → genus 0; L = 7, 8, 10, 14 → genus 4. All four handles live inside
   |x|,|y|,|z| < 6, i.e. entirely within the junction, not out on the arms.
2. **Octant knock-out.** Removing the material beyond 2.5 mm in one octant at a time:
   removing the (+x,+y,−z) octant drops genus 4 → 2, and removing (+x,−y,−z) drops it
   4 → 2. The other six octants leave genus 4 untouched.
3. **Ball probe.** Intersecting with a 3.5 mm ball centred on each of the eight triple points
   (±4.243, ±4.243, ±4.243): only (+4.243,+4.243,−4.243) and (+4.243,−4.243,−4.243) contain
   topology at r = 2.0. At r = 1.5 the two loaded corners are instead
   (+4.243,+4.243,+4.243) and (+4.243,−4.243,+4.243) — two corners, one handle each,
   giving genus 2.

A near-self-contact scan of the mesh (`probe.py pinch`, vertex pairs close in space but ≥ 5
edges apart on the surface) puts the necks at (4.741, −5.735, −3.675) — 0.18 mm gap — and at
(5.865, 4.518, −3.852) — 0.42 mm gap, exactly the two corners the knock-out found.

**In plain language.** The handles are not on the arms and not in the middle of the solid.
They sit in the eight concave pockets where three arms meet — the body-diagonal corners at
about (±4.2, ±4.2, ±4.2). Along each pair of cylinders runs a concave crease, and the fillet
lays a bead of material along it. Near a corner, three of those beads run into one another.
When the radius is small the beads pass each other with a gap and the pocket stays open. As
the radius grows each bead fattens until two adjacent beads touch and fuse across the mouth
of the pocket, welding a thin web of material over a gap that is still open underneath — and
a web over a gap is a tunnel. `tunnel_r2.0_section.png` is a 0.15 mm slice straight through
one of them: a hole roughly 0.3 × 0.4 mm, completely ringed by material, in a part whose arms
are 12 mm across. The same slice at r = 1.0 (`tunnel_r1.0_section.png`) is solid. Each new
handle is one bead-to-bead bridge, so the count rises with the radius: 0 at r ≤ 1.0, two
bridges at r = 1.5, four at r = 2.0.

Two caveats worth writing down. First, the effect is **not symmetric** even though the model
is: only two of the eight identical corners bridge, and *which* two changes with the radius
(the +z pair at 1.5, the −z pair at 2.0). That is the crease-dropping in the warnings — the
builder refuses a different subset of creases in each corner, so nominally identical corners
end up with different beads. Second, the tunnels are **sub-millimetre**. They are real
topology and they are what the χ measures, but they are not visible in a whole-part render;
you have to section the corner to see them, which is why the slab image and not the diagonal
cutaway is the evidence here.

---

# The second component at r = 0.9

The sweep records `cross` at r = 0.9 as `comp=2`, VALID, χ = 4, genus 0. That is real and it
reproduces on this binary.

**Reproducibility.** Five identical renders of r = 0.9 all returned comp = 2 with the same
fragment at the same coordinates. This one is deterministic, not a flake.

**Weld-tolerance sensitivity — none.** comp = 2 at 1e-4, 1e-6 and 1e-9 alike, and the
fragment's own face and vertex counts do not move between them. It is not a single-vertex
touch being read two ways; the fragment shares no vertex with the main body at any tolerance
tried.

    cross_r0.9.stl  VALID v=1343 e=4017 f=2678 comp=2 bnd=0 nonman=0 chi=4 genus=0  tol=1e-4
    cross_r0.9.stl  VALID v=1346 e=4026 f=2684 comp=2 bnd=0 nonman=0 chi=4 genus=0  tol=1e-6
    cross_r0.9.stl  VALID v=1346 e=4026 f=2684 comp=2 bnd=0 nonman=0 chi=4 genus=0  tol=1e-9

**1. Size.** The second component is a single **tetrahedron**: 4 triangles, 4 vertices.

    volume     3.454e-07 mm^3        (main component: 10 899.7 mm^3 — a ratio of 3e-11)
    bbox       x [4.5519, 4.5959]    0.0440 mm
               y [-5.0991, -4.9906]  0.1085 mm
               z [-3.8885, -3.8315]  0.0570 mm
    longest edge  0.122 mm
    thickness     two of its four vertices are 0.0039 mm apart — it is a needle-shaped sliver

    vertices:  (4.5519045, -4.9906407, -3.8885326)
               (4.5755380, -5.0990874, -3.8369612)
               (4.5955085, -5.0783826, -3.8314557)
               (4.5958707, -5.0749193, -3.8331829)

For scale against a part whose arms are 12 mm in diameter and 40 mm long: it is about one
ten-thousandth of the arm diameter in its longest dimension, and its volume is that of a cube
0.07 mm on a side. This is **a speck, not a piece of bead** — but it is a speck of geometry
the builder emitted, not a rounding artefact of the exporter (STL round-trips exactly).

**2. Where.** Centroid (4.5797, −5.0608, −3.8475), at radius 7.83 mm from the origin. That is
in the (+x, −y, −z) junction pocket, right on the crease between the y-arm and the z-arm and
close to the triple point (4.243, −4.243, −4.243) — the same class of location as the handles
above, and the same neighbourhood the "does not fit" warnings list. `frag_r0.9_where.png`
marks it on the whole part.

**3. Inside or outside.** **Outside — a stray shard, not a void.** The generalised winding
number of the fragment's centroid with respect to the main component is 0.0000 (a void would
read ±1). It sits just clear of the main surface: the point satisfies x² + z² = 35.8 against
the y-cylinder's 36, i.e. it is a hair outside the faceted y-cylinder wall, floating in the
concave pocket. `frag_r0.9_context.png` shows it beside the main surface clipped to a 0.25 mm
ball.

**4. Renders.** `frag_r0.9_isolated.png` is the fragment alone, viewed square-on to its own
plane in a 0.15 mm field — a needle. `frag_r0.9_context.png` and `frag_r0.9_where.png` place
it.

**5. Radius bracket.** Swept r = 0.6, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.2,
1.3 (weld 1e-6):

    comp=1 everywhere except  r = 0.90  and  r = 0.95,  which read comp = 2.

Both of those emit the same 4-face sliver at essentially the same place — r = 0.95 gives
volume 3.126e-07 mm³, centroid (4.5643, −5.0933, −3.8567), 0.03 mm from the r = 0.9 one. So
it is **a narrow band, not an isolated point and not a scattering**: the same near-degenerate
construction persists over a 0.05 mm window of radius and then goes away. That points at one
specific bead in one specific corner passing through a degenerate configuration as the radius
sweeps past it, rather than at random numerical noise.
