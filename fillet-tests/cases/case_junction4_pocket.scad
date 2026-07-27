// Four concave faces meeting at a point: a cube with a square-pyramid pocket
// ($fn = 4 cone) -> fillet_tool. One more face at the junction than
// case_junction3_pocket, which is where a single corner sphere stops being
// enough and several feasible solves have to be reconciled.
// No hand reference; the sandwich check carries the case.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;

R  = 30;
HC = 60;

CASE_SIGN  = "union";
CASE_SLICE = ["stack", [6, 20, 34, 48, 56], 90];
CASE_DY    = 5 * 90 + 60;
//                name     size kind    tol
CASE_VARIANTS = [["small",   6, "none", 0.12]];

module case_model() {
  difference() {
    translate([-R, -R, 0]) cube([2 * R, 2 * R, HC]);
    cylinder(r1 = R, r2 = 0, h = HC, $fn = 4);
  }
}

module case_ref_tool()  { }
module case_cand_tool() fillet_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
