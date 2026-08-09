# Theory: the L-bracket "trapezoid" side face / tapering bead

The vertical arm's flat side face reads as a trapezoid rather than a rectangle, and
the front-left round-over bead looks like it tapers from the top corner down to the
elbow. A theory of what causes that, and why most of it is not a fillet bug.

## Evidence

Measured on the S3-T01 tile, the projected width of the front-left round-over down
the vertical arm's straight edge (crease-to-silhouette, crease taken as the
luminance minimum, which is robust):

| position (tile height) | round-over width |
|---|---|
| 0.32–0.34 (corner cap) | 31–38 px |
| 0.36–0.68 (straight run) | **constant ~22 px** |
| 0.69–0.72 (elbow) | 34–42 px |

So the straight run does **not** taper — width is flat at ~22 px. The wide reading
at the very top is the convex corner cap; the widening at the very bottom is the
elbow. A genuine radius taper would show the width shrinking monotonically along the
straight run, and it does not.

## Theory of the two contributions

The trapezoid impression is two effects stacked, neither of which is a fillet-radius
taper:

**1. Perspective (global, a rendering choice — the dominant one).** `sheet.sh`
renders with `--projection=p`. Under perspective a rectangular slab face viewed at
an angle keystones into a trapezoid — parallel edges converge toward a vanishing
point, and the far end of the arm is drawn smaller than the near end. The silhouette
of the round-over leans with it (measured: the left silhouette drifts ~15 px right
over the straight run while the *width* stays constant — a lean, not a taper). This
is exactly the classic perspective keystone and is not geometry at all.

**2. Mixed-corner pull-in (local, real geometry — confined to the elbow).** At the
elbow the round-over meets the concave crease at a mixed corner. To un-twist that
corner, `pullStation` / `stripFoot` seat the strip's end cross-section *back* along
the edge (clamped to ≤ 0.45·edge length), and the corner patch fills between the
pulled foot and the true vertex. So within a short stretch above the elbow the
round-over retreats and the flat face flares wider — the measured 22 → 42 px jump in
the last ~5 %. This is a real, *intended* consequence of the pull-in seating
(commit "un-twist mixed corners (pull-in seating)"), not a radius error; it just
happens to make the face non-rectangular right at the corner.

There is a third thing it is **not**: an along-sweep taper. Long straight creases are
split by the "along-sweep station floor" specifically so a strip interpolated
between its two end sections cannot taper. The constant-width measurement over the
whole straight run says that floor is doing its job here — if it were failing, the
width would ramp between the two ends, and it is flat.

## Net

- The bead does **not** taper along the straight edge (measured constant).
- The trapezoid is **mostly perspective** (a `--projection=p` artifact), plus a
  **real but local** flare at the elbow from the mixed-corner pull-in.
- Neither is a fillet-radius bug. The elbow flare is a cosmetic side effect of the
  same mixed corner whose saddle is analysed in `lbracket-saddle-theory.md`; if that
  corner is reworked, the local flare is worth re-checking at the same time.

## The one check that settles it

Re-render S3-T01 with `--projection=o` (orthographic). Prediction:

- the straight run squares up (the global trapezoid disappears) → perspective
  confirmed for contribution 1;
- a small local flare remains right at the elbow → the pull-in, contribution 2.

If instead a taper persists along the whole straight run under orthographic
projection, this theory is wrong and it is a real radius/along-sweep bug after all.
