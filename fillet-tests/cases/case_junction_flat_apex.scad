// A very flat three-face apex -> round_tool. A probe rather than a case with an
// expected answer: how flat does an apex have to get before the corner solve's
// determinant guard fires, and does anything go wrong before it does?
//
// The determinant here is around 1e-2, four orders above the 1e-6 threshold, so
// the guard should NOT fire and the corner should come out fine. Reaching 1e-6
// on a cone needs a slant within about 0.04 degrees of the base — 60 wide and
// 0.02 tall — which no radius fits inside anyway.
//
// If that holds, the finding to record is that the guard is unreachable by any
// shape a user would write, and the question becomes whether it guards the right
// quantity at all: a threshold on the raw determinant is scale-free but says
// nothing about r, while what actually matters is whether the solved centre is
// USABLE — which is what the |P - v| > 10r bound already asks, and which
// case_junction_needle reaches instead. Recorded here so the next person does not
// go looking for a shape that trips the determinant.
//
// FOUND: that is what happens. The guard does not fire, the corner is built, no
// warning is emitted, and the applied result is a clean manifold solid. The tip
// stays at z = 2 and that is correct rather than a missed round: a ball tangent
// to three planes this close to horizontal tops out level with the apex it
// replaces. So the determinant guard remains unreached by anything buildable,
// and the question it raises is the one above — whether a scale-free threshold
// that says nothing about r is guarding the right quantity.
//
// "sandwich" fails, and not because of any of that. What is flat here is also
// SHARP: the rim where the slant meets the base is a 1.9-degree knife edge, and
// rounding a knife edge of angle t with radius r sets the surface back by
// r/tan(t/2) — about 36 mm at r = 0.6, sixty radii. The applied solid measures
// x -12.3 .. 24.6 against the model's -30 .. 60. So the result is far outside
// its own size of the model while being perfectly correct, which is the
// documented coarseness of a reference-free check rather than a defect. Same
// kind of failure as case_junction3_apex: the check cannot express the answer.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

R = 60;     // base radius
H =  2;     // height — the whole point: a slant only ~1.9 degrees off the base

CASE_SIGN  = "subtract";
CASE_SLICE = ["stack", [0.2, 0.7, 1.2, 1.7], 130];
CASE_DY    = 4 * 130 + 130;
//                name     size kind    tol
CASE_VARIANTS = [["small", 0.6, "none", 0.012]];

module case_model() cylinder(r1 = R, r2 = 0, h = H, $fn = 3);

module case_ref_tool()  { }   // none: see case_junction3_pocket
module case_cand_tool() round_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
