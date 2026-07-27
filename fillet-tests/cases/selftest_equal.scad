// Harness self-test, positive. The "candidate" is not the operator at all: it is
// the same hand-written bead as the reference, tessellated differently ($fn 48
// vs 96). Every check must PASS, which is what proves the comparison tolerates
// tessellation and is not trivially always-failing.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;

H  = 40;
CX = 10;
CY = 10;

CASE_SIGN  = "union";
CASE_SLICE = ["top", H / 2];
CASE_DY    = 120;
//               name    size  has_ref  tol
CASE_VARIANTS = [["small",  6,  true,   0.12]];

module case_model() {
  cube([40, CY, H]);
  cube([CX, 40, H]);
}

module case_ref_tool()  ref_fillet_edge_z(CX, CY, $case_size, H, $fn = 96);
module case_cand_tool() ref_fillet_edge_z(CX, CY, $case_size, H, $fn = 48);
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
