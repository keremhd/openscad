// Same L as case_inner_corner_fillet, cut with chamfer_tool instead: a flat
// wedge, no arc. Sole concave edge, so no clipping needed.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;

H  = 60;
CX = 10;
CY = 10;

CASE_SIGN  = "union";
CASE_SLICE = ["top", H / 2];
CASE_DY    = 120;
//               name    size  has_ref  tol
CASE_VARIANTS = [["small",  6,  true,   0.12],
                 ["large", 35,  false,  0.70]];

module case_model() {
  cube([40, CY, H]);
  cube([CX, 40, H]);
}

module case_ref_tool()  ref_chamfer_edge_z(CX, CY, $case_size, H);
module case_cand_tool() chamfer_tool(t = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
