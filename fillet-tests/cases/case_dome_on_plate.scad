// A hemispherical dome on a plate -> fillet_tool: a crease whose wall is curved
// in BOTH directions. Every other curved case here rides a cylinder, where one
// of the two principal curvatures is zero.
//
// This is the shape that found the size gate's contact test wrong. The gate
// used to place the ball's tangency point by stepping off the ball centre along
// an averaged wall normal, which assumes the wall is flat, so on a doubly
// curved wall the point it produced sat off the surface by the sagitta,
// r^2 / 2R — 0.0625 at r = 1 on this 8 mm dome, 0.25 at r = 2 — and the gate
// refused the crease for leaving a surface it had never left. A cylindrical
// boss of the same radius passed, because the step ran along the ruling where
// there is no curvature. The fix was to stop constructing the point at all and
// take the nearest point on the wall's own triangles, which lies on the dome by
// construction; the two small variants here are that fix pinned against
// regression, at exactly the radii that were refused.
//
// The oversize variant is the other end of the same axis, and it is not what
// the queue predicted. A dome was expected to run out of constant-radius
// solutions once the blend radius approached the curvature it has to follow.
// It does not: on a plate wide enough, this dome takes r = 30 on a radius of 8
// without a word, and correctly so — the crease is a circle in the plate's own
// plane and the ball seated in it meets the plate one radius outside that
// circle, which always exists. What runs out is the PLATE. The crease's own
// radius is 7.9829 rather than 8, the 48-gon equator's inscribed one, and the
// plate reaches 20 from the axis, so the contact leaves it at r = 12.017 — and
// above that the overshoot the warning reports is r - 12.017 to five figures at
// every size probed, which is the plate's arithmetic and nothing else's. So the
// refusal this case pins is the plate's edge, honestly named, and the dome's
// curvature is measured to cost nothing at all.
//
// No hand reference. The blend is a torus swept round the ring, but the wall it
// runs out onto is a sphere, and where the bead stops is not writable as the
// annulus-minus-torus idiom the boss and hole-mouth cases use.

include <../lib/_ref.scad>;
include <../lib/_harness.scad>;

$fn = 24;   // fillet arcs; see case_hole_mouth_round on why this is not 64

PW = 40;    // plate width and depth
PT = 10;    // plate thickness, top face at z = PT
RD = 8;     // dome radius; it sits on its equator, so the crease is a circle of
CX = 20;    // RD in the plate's top face, centred on the plate
CY = 20;

CASE_SIGN  = "union";
CASE_SLICE = ["front", CY];   // a cut through the axis: the rho-z profile, with
                              // the bead on both sides of the dome
CASE_DY    = 0;
CASE_DZ    = PT + RD + 14;    // rows step in Z so every one stays in the cut
//                name        size kind    tol
CASE_VARIANTS = [["small",       1, "none", 0.02],
                 ["large",       2, "none", 0.04],
                 ["oversize",    14, "drop", 0.28]];

module case_model() {
  cube([PW, PW, PT]);
  translate([CX, CY, PT]) sphere(r = RD, $fn = 48);
}

module case_ref_tool()  { }   // none: see the header
module case_cand_tool() fillet_tool(r = $case_size) case_model();

// The base ring is the model's only concave crease, so the comparison would be
// correct unclipped — but dilating the whole plate and dome through CGAL is
// where the time goes. Clip to the annular chunk the ring lives in, sized for
// the largest radius that BUILDS: the oversize variant emits nothing, so a clip
// that grew with it would only make its "sandwich" dilate the whole part to
// compare it with itself.
module case_clip() translate([CX, CY, PT - 6]) cylinder(r = RD + 6, h = 12);

module case_view()
  fillet_case_view(CASE_SIGN, CASE_VARIANTS, CASE_SLICE, CASE_DY, dz = CASE_DZ)
    { case_model(); case_ref_tool(); case_cand_tool(); case_clip(); }

if (!FILLET_DRIVER) case_view();
