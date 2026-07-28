// A long thin spike -> round_tool: the junction whose corner solve is REFUSED
// while its spines are truncated anyway.
//
// The apex half-angle here is tiny, so a ball seated against all three slant
// walls sits far down the axis — well past the |P - v| > 10r sanity bound the
// solve applies. The junction therefore gets no centre, and nothing tells
// truncation about it: every incident spine still stops short of the apex, and
// nothing fills the space it vacated.
//
// Of the two rejection paths, this is the reachable one. Three DISTINCT planes
// that fail to pin a point down would have to share a direction, and three
// planes sharing a direction and a point share the whole line through it — an
// edge, not a vertex. The determinant guard is therefore defensive against
// near-degeneracy and against a bad triple at valence four and up, not something
// a valence-three corner can walk into; see case_junction_flat_apex.
//
// FOUND, at r = 1 on a 3 x 120 spike: the three slant beads stop at z = 40 and
// nothing fills the 80 mm above them — eighty radii of spine cut away, and a
// fully sharp spike left standing over the stumps. No warning is emitted. The
// result is at least a valid solid: the applied difference comes out manifold
// and genus 0, so this is an under-fill, not a broken mesh.
//
// The checks barely see it. "emits" passes on the beads that do exist, and
// "sandwich" cannot run here at all: dilating a 120 mm needle through CGAL's Nef
// kernel was still going at twenty minutes of CPU, so it is recorded as a
// TIMEOUT rather than a verdict. The picture and this note are the coverage
// until the runout fallback exists; this case is its acceptance.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

R = 3;      // base radius
H = 120;    // height

CASE_SIGN  = "subtract";
// Sections bracketing z = 40, where the beads stop: two below, two above, one
// near the tip. The spacing is a few section-widths and no more — the sections
// here are millimetres across, and a stack spaced for a 30 mm cone would draw
// them as specks in an empty picture.
CASE_SLICE = ["stack", [10, 30, 38, 45, 90], 9];
CASE_DY    = 5 * 9 + 20;
CASE_DX    = 14;   // column pitch, for the same reason as the spacing above
//                name     size kind    tol
CASE_VARIANTS = [["small",   1, "none", 0.02]];

module case_model() cylinder(r1 = R, r2 = 0, h = H, $fn = 3);

module case_ref_tool()  { }   // none: see case_junction3_pocket
module case_cand_tool() round_tool(r = $case_size) case_model();
module case_clip()      fillet_clip_all();

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY, dx = CASE_DX)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
