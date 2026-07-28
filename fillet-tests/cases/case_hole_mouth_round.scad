// Mouth of a through hole in a plate: a CLOSED convex ring edge -> round_tool.
// Closed-chain coverage, complementary to the open chain of the L cases.
//
// Sliced vertically through the axis, so the rho-z profile and both sides of the
// ring show in one image. Clipped to a cylinder around the mouth: the operator
// also rounds the far rim of the hole and the plate's outer edges, which this
// case is not about.
//
// The large variant reaches far enough across the top face to collide with the
// round on the plate's own outer edge — two different features' tools competing
// for the same material, which is refused with a warning rather than resolved by
// shrinking either one: the "drop" kind. Note the checks stay inside case_clip(),
// so this reads the mouth alone; the plate's other edges are free to emit.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

// Arcs only need to beat the 2% tolerance, and the checks pay dearly for every
// facet past that: the dilation decomposes into convex parts through CGAL, which
// is where nearly all of a check's time goes and which grows far faster than the
// facet count — 132 parts at $fn = 16, 294 at 24, 1162 at 48. At 24 the chord
// error is 0.043 on r = 5 against a 0.10 tolerance, and the check runs in a
// fifth of the time.
$fn = 24;

W = 80;    // plate width
T = 30;    // plate thickness, top face at z = T
R = 12;    // hole radius

CASE_SIGN  = "subtract";
CASE_SLICE = ["front", 0];
CASE_DY    = 0;      // rows would overlap in a vertical cut; stack along Z instead
CASE_DZ    = T + 40;
//                name     size kind    tol
CASE_VARIANTS = [["small",   5, "ref",  0.10],
                 ["large",  24, "drop", 0.48]];

module case_model() {
  difference() {
    translate([0, 0, T / 2]) cube([W, W, T], center = true);
    translate([0, 0, -1]) cylinder(r = R, h = T + 2);
  }
}

module case_ref_tool()  ref_round_hole_mouth(R, T, $case_size);
module case_cand_tool() round_tool(r = $case_size) case_model();

// Cylinder around the mouth: out to 2 r of top face, down 2 r into the hole.
module case_clip() {
  c = 2 * $case_size;
  translate([0, 0, T - c]) cylinder(r = R + c, h = c + 1);
}

// Variants stack along +Z rather than +Y so both stay in the y = 0 cut plane.
module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY, dz = CASE_DZ)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
