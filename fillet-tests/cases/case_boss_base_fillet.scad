// Base of a cylindrical boss standing on a plate: a CLOSED concave ring edge ->
// fillet_tool. The concave counterpart of the hole mouth.
//
// It is the model's only concave edge, so nothing needs clipping, and both
// radii fit the available faces — this is the one case where a large radius
// still has a hand reference to check against.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // see case_hole_mouth_round: the dilation's cost is all in the
            // convex decomposition, which grows much faster than the facet count

W  = 80;    // plate width
T  = 15;    // plate thickness, top face at z = 0
RB = 12;    // boss radius
HB = 40;    // boss height

CASE_SIGN  = "union";
CASE_SLICE = ["front", 0];
CASE_DY    = 0;
CASE_DZ    = HB + 20;
//                name     size kind   tol
CASE_VARIANTS = [["small",   5, "ref", 0.10],
                 ["large",  24, "ref", 0.48]];

module case_model() {
  translate([0, 0, -T / 2]) cube([W, W, T], center = true);   // plate z[-T,0]
  cylinder(r = RB, h = HB);                                    // boss  z[0,HB]
}

module case_ref_tool()  ref_fillet_boss_base(RB, 0, $case_size);
module case_cand_tool() fillet_tool(r = $case_size) case_model();

// The base ring is the only concave edge, so the comparison would be correct
// unclipped — but dilating the whole plate-and-boss through CGAL does not
// finish, so clip to the annular chunk the case is about, with a full radius of
// margin around the bead.
module case_clip() {
  c = 2 * $case_size;
  translate([0, 0, -c]) cylinder(r = RB + c, h = 2 * c);
}

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY, dz = CASE_DZ)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
