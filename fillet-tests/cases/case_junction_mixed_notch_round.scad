// case_junction_mixed_notch's model under round_tool: the same notched block,
// the same two vertices, the other tool.
//
// The selection is now the convex edges, so at (25, 25, 20) it is the notch's
// concave crease the corner solve never hears about, and the all-concave floor
// corner at (25, 25, 8) drops out of the selection entirely — where the fillet
// version had a working junction next to a mixed one, this one has a mixed
// junction and nothing to compare it against locally. That asymmetry is why both
// exist.
//
// FOUND: clean. The round tool puts nothing inside the notch's concave floor
// corner, which is the gouge this pairing was written to catch. Both checks pass
// and the case carries no expectation line.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

B = 40;
N = 20;
X = 25;
Z =  8;

CASE_SIGN  = "subtract";
CASE_SLICE = ["front", 32];
CASE_DY    = 120;
//                name     size kind    tol
CASE_VARIANTS = [["small",   3, "none", 0.06]];

module case_model() {
  difference() {
    cube([B, B, B / 2]);
    translate([X, X, Z]) cube([N, N, N]);
  }
}

module case_ref_tool()  { }   // none: see case_junction_mixed_notch
module case_cand_tool() round_tool(r = $case_size) case_model();
module case_clip()      translate([14, 14, -1]) cube([28, 28, 23]);

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
