// A branch pipe of the SAME radius as the run, crossing it -> fillet_tool: the
// shape whose two seam loops cross each other, and the only one that has that.
//
// case_pipe_tee_fillet left this behind as the hard half. The branch runs along
// X here, so the two surfaces are x^2 + y^2 = Rr^2 and y^2 + (z-Zb)^2 = Rb^2 and
// the seam solves to (z-Zb)^2 = x^2 - (Rr^2 - Rb^2). A branch narrower than the
// run makes that negative near x = 0: the two loops start only where it turns
// positive and never touch. Only at equal radii does the constant vanish,
// leaving z - Zb = +-x — two ellipses meeting at (0, +-R, Zb). There is no
// branch radius that makes them cross at an angle. Equality is what creates the
// crossing.
//
// FOUND: and equality is what destroys it. The two cylinders share a tangent
// plane where the ellipses meet, so the seam's dihedral runs to zero on the way
// in, drops under the crease threshold well before it arrives, and is cut there
// like any other shallow feature. The chains that come back are FOUR OPEN ARCS,
// not two crossing loops, and the crossings are not junctions because by the
// time a spine reaches them there is no spine. Tessellating finer does not
// recover them; tangency is the reason the loops cross in the first place.
//
// So the valence-four junction on a curved crease that this shape was queued to
// provide is not in it. That gap is still open, and a shape that closes it has
// to have its curved creases meeting at an angle — two bosses overlapping on a
// plate would — rather than at a tangency.
//
// The chain count is pinned exactly in FilletBuilder_test.cc, where it is four
// numbers rather than a render. What this case is for is the other half: what
// the four arcs LOOK like as they run out toward each other, whether the notch
// they leave at the tangency reads as a defect or as the only honest answer, and
// whether four runouts converging on one point stay a valid solid.
//
// One size. The question here is topological, and a second radius would answer
// it a second time at a full check's cost; r = 2 matches its sibling case so the
// two pictures can be read against each other.
//
// No hand reference, as for every curved crease. That leaves "sandwich" and the
// picture, and here "sandwich" cannot be taken either: the tangency the four
// spines run out onto leaves triangles too small for CGAL to quantise, so the
// dilation refuses the candidate. It is the pocket cases' red line a third time
// and it is not about this solid — the same model with the branch along Y
// instead of X reports the same counts, comes back NoError and genus 0, and
// dilates. See expectations.txt. So the picture is the coverage here, alone.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

RR = 10;    // both radii, and they must stay equal — see the header
HR = 60;    // run length
LB = 30;    // branch half-length, so the branch crosses rather than stubs
ZB = 30;    // branch height on the run

CASE_SIGN  = "union";
// Sections climbing from the crossings upward. At z = ZB the branch's strip is
// exactly tangent to the run's disc and the section has no concave corner at
// all — that IS the tangency, drawn. Each section above it has four, opening
// wider as they go, and the bead is present in those and absent in the first
// two. The branch runs along X precisely so this stack can step along +Y.
CASE_SLICE = ["stack", [ZB, ZB + 2, ZB + 4, ZB + 6, ZB + 9], 40];
CASE_DY    = 5 * 40 + 60;
//                name     size kind    tol
CASE_VARIANTS = [["small",   2, "none", 0.04]];

module case_model() {
  cylinder(r = RR, h = HR, $fn = 48);
  translate([0, 0, ZB]) rotate([0, 90, 0]) translate([0, 0, -LB])
    cylinder(r = RR, h = 2 * LB, $fn = 48);
}

module case_ref_tool()  { }   // none: see the header
module case_cand_tool() fillet_tool(r = $case_size) case_model();

// The seams are the model's only concave creases, so nothing needs selecting
// out — but dilating the full 60 mm run and 60 mm branch through CGAL is where
// every check's time goes. Clip to the block the two seams live in.
module case_clip()
  translate([-RR - 2, -RR - 2, ZB - RR - 2]) cube([2 * RR + 4, 2 * RR + 4, 2 * RR + 4]);

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
