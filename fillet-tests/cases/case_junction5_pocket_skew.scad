// The concave form of case_junction5_apex_skew: the same sheared five-sided
// pyramid cut OUT of a block -> fillet_tool.
//
// Same reasons, same size limit (r = 2, see the apex case). The block runs past
// the tip rather than stopping flush with it: with the two flush the solid
// pinches to a single point at that vertex — two boundary sheets meeting at one
// vertex — and CGAL's convex decomposition, which every dilation in the checks
// goes through, segfaults there. Nothing about the pocket changes; the block
// just does not stop at the tip. See case_junction3_pocket, which is where that
// was found.
//
// FOUND: as with case_junction3_pocket and case_junction4_pocket, "sandwich"
// fails because CGAL will not dilate the applied result — a sphere sits tangent
// to three walls at the corner, and the surfaces meeting along those tangencies
// leave triangles too small to survive the Nef kernel quantising its input. It
// is not a statement about the corner geometry, and reducing that tangential
// contact is what would turn all three green.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

R   = 30;
HC  = 60;
CAP =  5;   // material above the apex — see the header
SKEW = [[1, 0, 0.45, 0], [0, 1, 0.20, 0], [0, 0, 1, 0]];

CASE_SIGN  = "union";
CASE_SLICE = ["stack", [6, 20, 34, 48, 56], 90];
CASE_DY    = 5 * 90 + 60;
//                name     size kind    tol
CASE_VARIANTS = [["small",   2, "none", 0.04]];

// The block covers the sheared cone's whole footprint with 5 to spare. The
// shear carries the axis to (27, 12) by the apex, but it carries the cone's
// radius to nothing on the way, and the two cancel: the widest section is still
// the base circle, so the footprint never leaves +/- R.
module case_model() {
  difference() {
    translate([-R - 5, -R - 5, 0]) cube([2 * R + 10, 2 * R + 10, HC + CAP]);
    multmatrix(SKEW) cylinder(r1 = R, r2 = 0, h = HC, $fn = 5);
  }
}

module case_ref_tool()  { }   // none: see case_junction3_pocket
module case_cand_tool() fillet_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
