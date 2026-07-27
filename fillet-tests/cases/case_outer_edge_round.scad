// Outer vertical edge of a cube, convex -> round_tool (the caller subtracts).
//
// Unlike the concave cases, a solid has no shortage of convex edges: the
// operator rounds all twelve of the cube's, while the hand reference covers one.
// The case therefore CLIPS the comparison to a box hugging the (S,S) edge, well
// clear of the top and bottom rims, so the check is about that one edge and not
// about the eight corners (which have no closed-form reference at all).
//
// The large variant fits its own face — r = 30 reaches back only 30 of the 40 mm
// available — but the neighbouring edge's round reaches 30 the other way, so the
// two overlap across the middle 20 mm and neither surface survives intact. It is
// the "nearest other feature" half of the size limit rather than the face-length
// half, and it is refused the same way: a warning and no tool, the "drop" kind.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;

S = 40;    // cube side
H = 60;    // height along Z

CASE_SIGN  = "subtract";
CASE_SLICE = ["top", H / 2];
CASE_DY    = 100;
//                name     size kind    tol
CASE_VARIANTS = [["small",   6, "ref",  0.12],
                 ["large",  30, "drop", 0.60]];

module case_model() cube([S, S, H]);

module case_ref_tool()  ref_round_edge_z(S, S, $case_size, H);
module case_cand_tool() round_tool(r = $case_size) case_model();

// A box around the target edge only: 1.2 r of face either side, and z well
// inside the rims the operator also rounds.
module case_clip() {
  c = 1.2 * $case_size;
  translate([S - c, S - c, 15]) cube([2 * c, 2 * c, H - 30]);
}

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
