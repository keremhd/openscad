# Round-over collision probes

Six models that drive the blend through genuine round-over collisions --
the crowding regime the corpus's elbow_crowded/elbow_thin_arm never
reach. run.sh sweeps r through each collision point (sweep.tsv);
fine.sh steps +-0.01 around each exact equality (fine.tsv). 58 cells,
none INVALID, two clean refusals (c3 r=3, c6 r=6).

Taxonomy measured 2026-08-17 (binary 9487c1604):

| model | collision | behaviour through the sweep |
|---|---|---|
| c1_thin_wall | round-overs meet through a 2-thick arm | tube=2 except exactly 2R = thickness, where the wall's round-overs are silently dropped (sharp edges, valid) |
| c2_parallel_creases | facing concave creases, 4-wide channel | tube -> saddle/fan -> coons as 2R crosses the width; trough facet noise at the top end |
| c3_thin_web | convex vs concave across a 3 web | tube except exactly 2R = web (saddle); REFUSED at 2R = 2x web |
| c4_endface_eaten | end-face round-over eats a 5-tall face | tube -> saddle at exactly 2R = height -> coons above; small end notch, valid |
| c5_narrow_slot | four slot edges collide at once | tube -> fan at 2R = width; slot degenerates to a V notch, valid |
| c6_over_round_cube | global over-round of a cube | capTri throughout; 2R = side gives a clean faceted sphere; past it, flat face flaps protrude through a lumpy ball (self-overlapping, topologically VALID, zero folds); REFUSED only at 2R = 2x side |

Two owner-level observations:
- Exact-equality cells (2R == feature) are a distinct dispatch behaviour,
  not noise: the tube path drops out and vertex counts collapse; +-0.01
  neighbours are normal.
- Over-round has no size gate: c6's self-overlapping flaps are invisible
  to every topological check (0 folds). If a refusal rule is ever wanted,
  radius-vs-body is the place; documented here, deliberately not built.
