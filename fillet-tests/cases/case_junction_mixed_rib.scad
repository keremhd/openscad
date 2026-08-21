// A rib standing on a plate, stopping short of the plate's edge -> fillet_tool.
//
// The vertex this case is about is (10, 18, 6), where the rib's free end meets
// the plate: two CONCAVE creases arrive there — the rib's flank against the
// plate and the rib's end against the plate — and so does the rib's own vertical
// edge, which is CONVEX. Valence three, two signs.
//
// The corner solve collects its constraint walls from the selected creases only,
// and fillet_tool walks concave edges, so the convex edge's far face is never
// heard of even though it bounds where the ball may sit. The sign flag applied
// to the whole junction is meaningless at such a vertex too.
//
// No hand reference: an equal-radius blend at a vertex has no closed form. What
// the case has is the sandwich check and the picture, and the picture is the
// important half here — a gouge at one corner is well within what sandwich
// tolerates.
//
// FOUND: clean, at this size. The two failures the case was written to catch —
// the foot fillet running past the end of the rib into open air, and the rib's
// vertical edge being eaten where the foot fillet's ball reached into it — do
// not happen. The bead turns the corner as a closed loop around the rib's foot,
// and no tool material lies further than r beyond the rib's end face. Both
// checks pass, so there is no expectation line for this case; the mixed sign at
// the vertex costs nothing here because the convex edge's far face is the rib's
// own end face, which the two concave creases already constrain the ball
// against.
//
// case_junction_mixed_rib_round is the same model under round_tool; the two are
// worth reading side by side, so they carry the same clip.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

PL = [60, 40, 6];    // plate
RB = [30,  4, 20];   // rib, free at both ends
RO = [10, 18,  6];   // rib origin — the mixed vertex is this corner of it

CASE_SIGN  = "union";
CASE_SLICE = ["front", 20];   // through the rib's length
CASE_DY    = 120;
//                name     size kind    tol
CASE_VARIANTS = [["small", 1.5, "none", 0.03]];

module case_model() {
  cube(PL);
  translate(RO) cube(RB);
}

module case_ref_tool()  { }   // none: see the header
module case_cand_tool() fillet_tool(r = $case_size) case_model();

// A box around the rib's free end, held clear of the plate's own outer edges so
// the round version of this case compares the corner rather than the plate rim.
module case_clip() translate([2, 6, -2]) cube([26, 28, 34]);

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
