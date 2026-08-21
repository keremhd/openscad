// A concave crease running along a floor, with a second wall standing close by,
// parallel to it and not touching it -> fillet_tool.
//
// Truncation only happens at junctions, and a junction is a vertex where three
// or more selected creases meet. A crease that merely passes CLOSE to another
// face — no shared vertex, no crease between them — is never truncated, so its
// ball is free to roll straight through that face. Same class of error as the
// pre-corner-cell junctions, with no corner available to fix it.
//
// The two walls are 10 apart, so the two variants sit either side of the gap:
//
//   small  r = 3  fits between them; should be clean
//   large  r = 8  does not — the ball seated in the first crease is wider than
//                 the gap and reaches into the second wall
//
// The large one is "none" rather than "drop" on purpose, and stays that way.
// A "drop" asserts that the whole tool must come out empty, and one of the three
// creases here — the one on the far side of the second wall, with nothing within
// reach — is legitimately buildable at either size.
//
// FOUND, in two stages, and neither is the failure the case was written for.
//
// FIRST: the ball never rolls through the second wall. No tool volume ends up
// inside it. What happened instead was that the bead COLLAPSED. Measured on the
// section at y = 20, the first wall's bead was
//
//   r = 3   x 4.00 .. 7.00, z 5.00 .. 8.00   — exactly the quarter-round
//   r = 8   x 4.00 .. 8.00, z 5.00 .. 5.26   — a sliver a quarter of a mm tall
//
// and the same model with the second wall deleted gave z 5.00 .. 13.00 at
// r = 8, so it really was the neighbour that crushed it: not by rolling through
// the wall, but by rolling its own ball through this bead from the other side.
// Nothing caught it — "sandwich" only bounds how far the result may STRAY from
// the model and an under-fill strays nowhere, "emits" was satisfied by the
// sliver, and no warning was emitted.
//
// SECOND, and what the case shows now: the size gate refuses both of the creases
// facing each other across the 10 mm gap, and says so. A crease whose seated ball
// contains another crease's contact line is a crease competing for material that
// is already spoken for, which is the "nearest other feature" half of the size
// limit. At r = 3 the two beads have 4 mm between them and both are built; at
// r = 8 both are dropped and only the third is built. The picture is the two
// answers side by side.
//
include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

FL = [60, 40,  5];   // floor
WL = [ 4, 40, 25];   // wall the crease belongs to
GAP = 10;            // clear distance between the two walls

CASE_SIGN  = "union";
CASE_SLICE = ["front", 20];   // the model is prismatic along y
CASE_DY    = 0;               // a front cut sees Y as depth: rows step in Z
CASE_DZ    = 45;
//                name     size kind    tol
CASE_VARIANTS = [["small",   3, "none", 0.06],
                 ["large",   8, "none", 0.16]];

module case_model() {
  cube(FL);
  cube(WL);
  translate([WL[0] + GAP, 0, FL[2]]) cube(WL);
}

module case_ref_tool()  { }   // none: an equal-radius blend, and see the header
module case_cand_tool() fillet_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY, dz = CASE_DZ)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
