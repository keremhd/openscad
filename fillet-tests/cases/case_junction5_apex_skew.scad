// A SHEARED five-sided pyramid -> round_tool: an asymmetric high-valence apex.
//
// The correctness of this corner is already pinned numerically —
// FilletCompare_test.cc compares a sheared pyramid's apex against the exact
// rounded solid at four, five and six sides, both signs, and checks that the
// apex really does yield several distinct ball centres rather than collapsing to
// one. This case adds nothing to that; what it adds is the contour stack, which
// is the only way to SEE a multi-centre corner, and the only reason to build it.
//
// The shear is what makes it worth looking at: on a symmetric cone every centre
// coincides, so a collapsed corner and a correct one draw the same picture.
//
// r = 2 and no larger. This shape has a crease at 130 degrees, where the
// tangency setback is over twice the radius; past about 2 on a 30 mm feature the
// neighbouring beads collide and the result stops being the rounded solid. That
// is the oversize question, not a statement about the corner, and mixing the two
// into one case would hide both.
//
// FOUND: the corner is built and "emits" passes; "sandwich" fails for the reason
// case_junction3_apex already fails it — rounding a sharp apex moves the surface
// by more than the radius, so a check that bounds the result to within the
// tool's own size cannot pass on any apex, correct or not.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

R  = 30;    // base radius
HC = 60;    // height
SKEW = [[1, 0, 0.45, 0], [0, 1, 0.20, 0], [0, 0, 1, 0]];

CASE_SIGN  = "subtract";
CASE_SLICE = ["stack", [6, 20, 34, 48, 56], 90];
CASE_DY    = 5 * 90 + 60;
//                name     size kind    tol
CASE_VARIANTS = [["small",   2, "none", 0.04]];

module case_model() multmatrix(SKEW) cylinder(r1 = R, r2 = 0, h = HC, $fn = 5);

module case_ref_tool()  { }   // none: see case_junction3_pocket
module case_cand_tool() round_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
