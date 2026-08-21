// A branch pipe landing on a run pipe -> fillet_tool: the weld fillet, and the
// first case in the suite whose crease is a CURVE rather than a polyline.
//
// Everything the builder does per station — the frame, the setback, the seated
// ball — is asked at a different angle here at every station. The seam between
// two perpendicular cylinders is a closed space curve whose dihedral opens from
// about 90 degrees at the flanks, where the branch meets the run head-on, to
// much shallower at the crown and keel, where the two surfaces are nearly
// tangent to one another. The setback goes as r*tan(phi/2), so it grows without
// bound toward those two points while the radius stays put; that is the whole
// interest of the shape and the reason for two sizes rather than one.
//
// The branch is a STUB, stopping inside the run rather than crossing it. A
// branch that crossed would leave two separate seam loops, one per side, which
// is two easy cases rather than one hard one; and a branch of the same radius as
// the run would leave the two loops crossing each other at two points, which is
// a junction on a curved crease and deserves its own case rather than being
// smuggled in as a variant here.
//
// No hand reference: an equal-radius blend along a varying-dihedral curve has no
// elementary closed form. The sandwich check and the picture are the coverage,
// as for the junction cases.
//
// FOUND: clean at both sizes, and no expectation line. The seam comes through as
// one closed chain of 48 segments, no station of it is refused at either size,
// and the bead covers the whole curve — the seam spans z 23..37, and the tool
// measures z 21..39 at r = 2 and z 19..41 at r = 4, which is the seam plus the
// radius at both ends, top and bottom.
//
// What that does NOT establish is the blend's shape along the curve. "sandwich"
// bounds how far the result may stray from the model and nothing more, so it
// would pass a bead whose radius drifted with the dihedral, and the two places
// this shape is hardest — the crown and the keel, where the surfaces go nearly
// tangent and the setback runs away — are exactly where a drift would hide. The
// honest next step for this case is not another variant but a reference: the
// shrink-and-grow comparison FilletCompare_test.cc already uses on the apexes
// works on any solid and would pin the radius here.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

RR = 12;    // run radius
HR = 60;    // run length
RB =  7;    // branch radius
LB = 25;    // branch length, from the run's axis outward
ZB = 30;    // branch height on the run

CASE_SIGN  = "union";
// Sections climbing past the seam: the fillet is widest where the section cuts
// the flanks and pinches toward the crown, which is the varying dihedral made
// visible.
CASE_SLICE = ["stack", [20, 26, 30, 34, 40], 40];
CASE_DY    = 5 * 40 + 60;
//                name     size kind    tol
CASE_VARIANTS = [["small",   2, "none", 0.04],
                 ["large",   4, "none", 0.08]];

module case_model() {
  cylinder(r = RR, h = HR, $fn = 48);
  translate([0, 0, ZB]) rotate([-90, 0, 0]) cylinder(r = RB, h = LB, $fn = 32);
}

module case_ref_tool()  { }   // none: see the header
module case_cand_tool() fillet_tool(r = $case_size) case_model();

// The seam is the model's only concave crease, so nothing needs selecting out —
// but the run's two rims and the branch's end rim are convex edges the fillet
// never touches, and dilating the whole 60 mm run through CGAL is the expensive
// part of every check. Clip to the block around the seam.
module case_clip() translate([-RR - 2, -2, ZB - RR]) cube([2 * RR + 4, LB + 2, 2 * RR]);

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
