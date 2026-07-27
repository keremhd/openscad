// Three concave faces meeting at a point: a cube with a 3-sided pyramidal pocket
// ($fn = 3 cone) cut out of it -> fillet_tool.
//
// There is NO hand reference here on purpose. The equal-radius trihedral corner
// has no simple closed form; building it correctly is the whole point of the
// corner-solve milestone. What this case does have is the reference-free
// sandwich check, so the corner is not merely eyeballed: the result still has to
// stay within r of the model, which catches a gouge or a runaway solve even
// while no exact answer exists to compare against.
//
// Sliced as a contour stack — sections at rising z laid out along +Y — so the
// section collapsing toward the meeting point reads at a glance.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;   // fillet arcs; the pocket's face count is set on the cone itself

R  = 30;    // cone base radius
HC = 60;    // cone height

CASE_SIGN  = "union";
CASE_SLICE = ["stack", [6, 20, 34, 48, 56], 90];
CASE_DY    = 5 * 90 + 60;
//                name     size kind    tol
CASE_VARIANTS = [["small",   6, "none", 0.12]];

module case_model() {
  difference() {
    translate([-R, -R, 0]) cube([2 * R, 2 * R, HC]);
    cylinder(r1 = R, r2 = 0, h = HC, $fn = 3);
  }
}

module case_ref_tool()  { }   // none: see the header
module case_cand_tool() fillet_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
