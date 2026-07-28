// A rectangular notch cut into one corner of a block -> fillet_tool.
//
// The mixed-sign counterpart of case_junction_mixed_rib, by subtraction rather
// than addition, so the two signs sit on opposite sides of the same faces. Two
// vertices matter and they are in the same picture:
//
//   (25, 25, 20)  where the notch opens through the top face: the notch's
//                 vertical CONCAVE crease runs into two CONVEX top-face edges
//   (25, 25,  8)  the notch's own floor corner, all-concave — a junction the
//                 corner solve can and does handle
//
// Having the working corner and the mixed one side by side is the point: the
// question is not only whether the mixed vertex goes wrong but whether it drags
// its well-formed neighbour with it. Watch for the notch's inner corner filling
// correctly while the block's outer edge above it is eaten back, or vice versa.
//
// No hand reference — see case_junction_mixed_rib. case_junction_mixed_notch_round
// is the same model under round_tool, with the same clip.
//
// FOUND: clean. The bead runs the full height of the vertical crease, present
// both just above the notch floor and just under the top face, and no tool
// material lies above the top face — so the crease is neither cut short at the
// mixed vertex nor allowed to run out past it. The all-concave floor corner is
// unaffected. Both checks pass and the case carries no expectation line.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

B = 40;   // block side
N = 20;   // notch side
X = 25;   // notch origin in x and y — it opens through two faces
Z =  8;   // notch floor height

CASE_SIGN  = "union";
CASE_SLICE = ["front", 32];   // through the notch, past its inner wall
CASE_DY    = 120;
//                name     size kind    tol
CASE_VARIANTS = [["small",   3, "none", 0.06]];

module case_model() {
  difference() {
    cube([B, B, B / 2]);
    translate([X, X, Z]) cube([N, N, N]);
  }
}

module case_ref_tool()  { }   // none: see the header
module case_cand_tool() fillet_tool(r = $case_size) case_model();

// The notch and the block corner it opens through, with a radius of margin.
// The block's far faces are outside it, so the round version compares the two
// vertices above rather than the rest of the block's twelve edges.
module case_clip() translate([14, 14, -1]) cube([28, 28, 23]);

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
