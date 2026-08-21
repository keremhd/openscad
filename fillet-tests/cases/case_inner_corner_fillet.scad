// Inner 90-degree edge of an L (two cubes), edge along Z, concave -> fillet_tool.
//
// The L has exactly one concave edge, so the operator's whole output should be
// the one reference bead — no clipping needed.
//
// The large variant deliberately asks for r = 35 where each face is only 30 mm
// long. Both tangent points would land 5 mm past the end of their face, so no
// bead touches the model tangentially anywhere: there is no fillet to build, at
// any radius, without changing the one the caller asked for. The required
// behaviour is a warning and no tool, which is what the "drop" kind checks.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;

H  = 60;    // edge length along Z
CX = 10;    // inner corner x
CY = 10;    // inner corner y

CASE_SIGN  = "union";
CASE_SLICE = ["top", H / 2];
CASE_DY    = 120;
//                name     size kind    tol
CASE_VARIANTS = [["small",   6, "ref",  0.12],
                 ["large",  35, "drop", 0.70]];

module case_model() {
  cube([40, CY, H]);   // horizontal arm  x[0,40] y[0,10]
  cube([CX, 40, H]);   // vertical arm    x[0,10] y[0,40]
}

module case_ref_tool()  ref_fillet_edge_z(CX, CY, $case_size, H);
module case_cand_tool() fillet_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
