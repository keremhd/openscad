// Same cube edge as case_outer_edge_round, cut with bevel_tool: a flat wedge,
// no arc. Clipped to the one edge for the same reason.
//
// The large variant collides with the neighbouring edge's bevel exactly as the
// round does, and is refused the same way — the "drop" kind.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;

S = 40;
H = 60;

CASE_SIGN  = "subtract";
CASE_SLICE = ["top", H / 2];
CASE_DY    = 100;
//                name     size kind    tol
CASE_VARIANTS = [["small",   6, "ref",  0.12],
                 ["large",  30, "drop", 0.60]];

module case_model() cube([S, S, H]);

module case_ref_tool()  ref_bevel_edge_z(S, S, $case_size, H);
module case_cand_tool() bevel_tool(t = $case_size) case_model();

module case_clip() {
  c = 1.2 * $case_size;
  translate([S - c, S - c, 15]) cube([2 * c, 2 * c, H - 30]);
}

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
