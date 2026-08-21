// Harness self-test, negative. The "candidate" is a hand-written bead of the
// WRONG radius (8 against a reference 6), so the tool check must FAIL: the
// shapes differ by far more than the tolerance. Together with selftest_equal
// this pins the harness between always-pass and always-fail.
//
// The sandwich check still passes here, and that is the honest calibration of
// it: a bead of the wrong radius still hugs both faces, so it never strays
// further than the nominal size from the model. Sandwich catches gouges and
// runaway tools, not sizing errors — which is why cases keep a hand reference
// wherever one can be written.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;

H  = 40;
CX = 10;
CY = 10;
WRONG = 8;

CASE_SIGN  = "union";
CASE_SLICE = ["top", H / 2];
CASE_DY    = 120;
//                name     size kind   tol
CASE_VARIANTS = [["small",   6, "ref", 0.12]];

module case_model() {
  cube([40, CY, H]);
  cube([CX, 40, H]);
}

module case_ref_tool()  ref_fillet_edge_z(CX, CY, $case_size, H);
module case_cand_tool() ref_fillet_edge_z(CX, CY, WRONG, H);
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
