// case_junction_mixed_rib's model under round_tool: same rib on the same plate,
// same mixed-sign vertex at (10, 18, 6), the other tool.
//
// Here the selection is the convex edges, so it is the two concave creases at
// that vertex whose faces the corner solve never hears about — the mirror of the
// hole the fillet version probes, and the reason both are built rather than one.
//
// Convex edges are everywhere on a solid, so the clip matters: it is the same
// box as the fillet version, around the rib's free end and clear of the plate's
// outer rim, which the tool also rounds and which this case is not about.
//
// FOUND: clean, at this size. No tool volume reaches into the plate below the
// rib's foot, so the plate's top face is not gouged where the concave creases
// meet the rib's vertical edge, and the vertical edge's round does run all the
// way down to that face rather than stopping short of it. Both checks pass and
// the case carries no expectation line.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

PL = [60, 40, 6];
RB = [30,  4, 20];
RO = [10, 18,  6];

CASE_SIGN  = "subtract";
CASE_SLICE = ["front", 20];
CASE_DY    = 120;
//                name     size kind    tol
CASE_VARIANTS = [["small", 1.5, "none", 0.03]];

module case_model() {
  cube(PL);
  translate(RO) cube(RB);
}

module case_ref_tool()  { }   // none: see case_junction_mixed_rib
module case_cand_tool() round_tool(r = $case_size) case_model();
module case_clip()      translate([2, 6, -2]) cube([26, 28, 34]);

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
