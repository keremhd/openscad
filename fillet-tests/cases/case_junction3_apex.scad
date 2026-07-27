// The convex counterpart of case_junction3_pocket: the bare 3-sided pyramid
// ($fn = 3 cone), three convex faces meeting at an apex -> round_tool.
// No hand reference, same reasoning; the sandwich check still applies.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;

R  = 30;
HC = 60;

CASE_SIGN  = "subtract";
CASE_SLICE = ["stack", [6, 20, 34, 48, 56], 90];
CASE_DY    = 5 * 90 + 60;
//               name    size  has_ref  tol
CASE_VARIANTS = [["small",  6,  false,  0.12]];

module case_model() cylinder(r1 = R, r2 = 0, h = HC, $fn = 3);

module case_ref_tool()  { }   // none: see case_junction3_pocket
module case_cand_tool() round_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
