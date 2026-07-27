// A selection brush covering only PART of an edge: the L of the inner-corner
// case, filleted only where z < 30. Children 1+ of the tool node are brushes.
//
// The reference clips the finished bead with the same box, which gives a flat
// cap perpendicular to the spine — the correct answer, as opposed to clipping
// the swept ball, which would leave a scooped round end. That distinction is
// the whole reason this case exists, and it is exactly what the diff column
// shows if the operator gets it wrong.
//
// Contour stack: the lower sections are inside the brush and show the arc, the
// upper ones are past the cap and are sharp again.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 64;

H  = 60;
CX = 10;
CY = 10;
BRUSH_TOP = 30;   // fillet only where z < 30

CASE_SIGN  = "union";
CASE_SLICE = ["stack", [8, 22, 38, 52], 90];
CASE_DY    = 4 * 90 + 60;
//               name    size  has_ref  tol
CASE_VARIANTS = [["small",  6,  true,   0.12]];

module case_model() {
  cube([40, CY, H]);
  cube([CX, 40, H]);
}

module case_brush() translate([-10, -10, 0]) cube([200, 200, BRUSH_TOP]);

module case_ref_tool()
  intersection() {
    ref_fillet_edge_z(CX, CY, $case_size, H);
    case_brush();
  }

module case_cand_tool() fillet_tool(r = $case_size) { case_model(); case_brush(); }
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
