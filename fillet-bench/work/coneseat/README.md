# The ball-seating instruments

Two rules, measured over the same populations, plus the models that generate them.

- `seat.py` / `ball.py` / `battery.py` / `relief.py` / `step.py` / `whose.py` — the
  instrument STATE.md §4d's separation was measured with: every steep convex edge of a
  blended mesh, split by whether both its faces lie in the ORIGINAL target's planes
  (genuine) or neither does (blend-made), with the ball seated from the two face normals
  and the constructed perpendicular foot measured against the mesh.
  `battery.py <target.stl> <blend.stl> <R> <label>` reproduces 114 genuine / 72 blend-made
  on `cross` at r=0.5 and 114 / 91 at r=2.0.

- `cone.py` — the same populations under the mesh-seated angular rule (`reseat()` plus
  `r*sin(angle to the normal cone at the contact)`), the rule attempt 2 built and
  `042bf9dff` carries. `cone.py <target> <blend> <R> <label> [seg] [convex|concave] [nosurf]`.
  `seg` is the `smoothSurfaces` grouping angle; crease selection stays 46.

- `hbcheck.py <dir-of-hb_plain_D.stl>` — the known-answer check. `handblend_step`'s convex
  test edge must read `R(1-tan(DELTA/2)) - D` whenever the blend is not merged into its wall,
  and machine zero when it is. Generate the meshes with
  `OpenSCAD --enable=fillet -D MODE='"plain"' -D D=<d> -o hb_plain_<d>.stl --export-format asciistl
  fillet-bench/work/handblend/handblend_step.scad`.

- `m/` — the concave-pass-only and target-only .scad files the two above read.

Nothing here is run by `sweep.sh`. These are diagnosis, and they are in the tree because the
version that produced §4d survived only in a session scratchpad.
