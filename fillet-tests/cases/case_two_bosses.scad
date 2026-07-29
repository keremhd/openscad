// Two overlapping bosses on a plate -> fillet_tool: a junction whose incident
// creases are CURVES. It is the one structural configuration the suite had
// never been pointed at.
//
// Every other junction here is polyhedral — a vertex where straight spines
// meet. case_pipe_tee_equal was built to supply a curved one and turned out not
// to contain one: equal radii are what make two seam loops cross, and equal
// radii also give the two cylinders a shared tangent plane there, so the
// dihedral falls under the threshold before the crossing and the chains come
// back as four open arcs with no junction between them. A curved junction needs
// creases that meet at an ANGLE, which a tee cannot supply at any radius.
//
// Two bosses overlapping on a plate can. Each has a base ring — a closed curved
// concave crease — the two rings cross at two points, and the groove where the
// cylinders interpenetrate is a straight concave crease running up from each
// crossing. The bosses overlap by 6 of their 20 diameter, so the groove walls
// meet at about 91 degrees, nowhere near the tangency that emptied the tee.
//
// FOUND, and the junction is in it. The two closed rings do not survive: each
// is cut at both crossings into one open arc running from crossing to crossing
// around the outside of its own boss, so what comes back is four chains — two
// curved arcs and the two straight grooves — and both crossings are genuine
// valence-3 junctions. Three walls meet at each, the corner solve pins exactly
// one seated ball, and nothing is refused or run out at any size probed. The
// tool is one connected piece of genus 1, which is the answer the topology
// demands: the two arcs close into a loop through the two corners. The counts
// are pinned in FilletBuilder_test.cc, where they are exact and free.
//
// What that leaves for the picture is what a count cannot carry: what the
// corner cell looks like where two curved spines and one straight one close on
// each other, and whether the ring bead runs out into it cleanly or leaves the
// notch the tee's four runouts do.
//
// The bosses are 24-gons, not the 48 this case was drafted with, and that is a
// finding rather than a cost saving. At 48 the tool comes back as THREE pieces:
// the right one, plus a detached wafer at each junction, 0.076 x 0.061 x 0.005
// and 5.2e-06 of volume, sitting on the plate face where the two ring beads'
// outer edges cross. It is the wedge's own eps overshoot again, shed by the
// corner cell — the same corner-cell geometry the pocket cases are permanently
// red for, seen for the first time on a curved junction. At 24 there is no
// crumb and the genus is right, so the case is written where the operator is
// sound and the crumb is recorded here rather than pinned as correct. A 24-gon
// still turns 15 degrees a facet against a 22.5-degree threshold, so the ring
// is a curve to the classifier either way.
//
// One size. The question is topological, and a second radius asks it again at a
// full check's cost; r = 2 matches its two sibling curved-crease cases so the
// three pictures can be read against each other.
//
// No hand reference — a blend along a curved crease closing into a valence-3
// corner has no elementary closed form, which is what "none" is for. And
// "sandwich" cannot be taken either; see expectations.txt for what was measured
// and why it is the kernel rather than this solid. The picture is the coverage.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

PW = 60;    // plate width
PD = 40;    // plate depth
PT = 6;     // plate thickness, top face at z = PT
RB = 10;    // boss radius, equal for both — unequal radii still cross
HB = 20;    // boss height
XA = 22;    // boss centres, 14 apart: they overlap by 6 of the 20 diameter, so
XB = 36;    // the groove is a real 91-degree crease and not a tangency
YC = 20;

CASE_SIGN  = "union";
// Sections climbing away from the crossings, which sit at z = PT. The first is
// just above the plate, where both ring beads and both groove beads are present
// and the corner cells are between them; a ring bead ends at z = PT + r, so the
// last two sections carry the grooves alone. The bosses lie along X and the
// stack steps along +Y.
// The first slab clears the plate's top face rather than straddling it: a slab
// that ends exactly on a face intersects it in nothing, and what gets drawn is
// the renderer's opinion of a zero-thickness solid.
CASE_SLICE = ["stack", [PT + 0.5, PT + 1.1, PT + 1.7, PT + 2.6, PT + 6], 60];
CASE_DY    = 5 * 60 + 60;
//                name     size kind    tol
CASE_VARIANTS = [["small",   2, "none", 0.04]];

module case_model() {
  cube([PW, PD, PT]);
  translate([XA, YC, PT]) cylinder(r = RB, h = HB, $fn = 24);
  translate([XB, YC, PT]) cylinder(r = RB, h = HB, $fn = 24);
}

module case_ref_tool()  { }   // none: see the header
module case_cand_tool() fillet_tool(r = $case_size) case_model();

// The rings and the grooves are the model's only concave creases, so the
// comparison would be correct unclipped — but dilating the whole plate through
// CGAL is where every check's time goes. Clip to the block the two bosses stand
// in, with a full tool's reach of margin around the beads. The grooves are cut
// partway up, which the comparison does not mind: both sides are clipped with
// the same region, so the artificial cut faces coincide and cancel.
module case_clip() {
  c = 2 * $case_size;
  translate([XA - RB - c, YC - RB - c, 0])
    cube([XB - XA + 2 * RB + 2 * c, 2 * RB + 2 * c, PT + 2 * c]);
}

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
