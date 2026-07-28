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
// The large one is "none" rather than "drop" on purpose. A drop would assert
// that the operator must REFUSE this size, and that decision has not been made:
// refusing on nearest-other-feature grounds is the open half of the size-limit
// question. Until it is settled, the case records what happens rather than
// claiming what should.
//
// FOUND, and it is the opposite of the failure the case was written for. The
// ball never rolls through the second wall: no tool volume ends up inside it.
// What happens instead is that the bead COLLAPSES. Measured on the section at
// y = 20, the first wall's bead is
//
//   r = 3   x 4.00 .. 7.00, z 5.00 .. 8.00   — exactly the quarter-round
//   r = 8   x 4.00 .. 8.00, z 5.00 .. 5.26   — a sliver a quarter of a mm tall
//
// and the same model with the second wall deleted gives z 5.00 .. 13.00 at
// r = 8, so it really is the neighbour that crushes it. The bead on the far side
// of the second wall goes the same way, crushed there by the wall's own 4 mm
// thickness rather than by the gap.
//
// Nothing catches this. "sandwich" only bounds how far the result may STRAY from
// the model, and an under-fill strays nowhere; "emits" is satisfied by the
// sliver; no warning is emitted. So the operator neither fills the crease nor
// refuses the size — it quietly builds a fillet that is not one, which is
// exactly the case for settling what "refuse" means.

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
