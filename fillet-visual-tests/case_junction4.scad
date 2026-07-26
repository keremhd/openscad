// Case E — 4-face meeting point via a low-$fn cone ($fn = 4 -> square-pyramid
// apex). Same layout as case_junction3, one more face at the junction.
//   left  column: the pyramid          -> CONVEX 4-face junction (round_tool)
//   right column: cube minus pyramid    -> CONCAVE 4-face pocket   (fillet_tool)
//
// No hand reference blend: the symmetric 4-face corner collapses to a single
// corner sphere, asymmetric ones to a hull of feasible spheres (plan §6.3.1) —
// milestone M9. This visualises the section-evolution target for M9.

include <_fillet_ref.scad>;

$fn = 4;            // 4-face apex (square pyramid)

R  = 30;
Hc = 60;
zs = [6, 20, 34, 48, 56];

module pyr_up() { cylinder(r1 = R, r2 = 0, h = Hc); }

module pocket() {
  difference() {
    translate([-R, -R, 0]) cube([2 * R, 2 * R, Hc]);
    pyr_up();
  }
}

fillet_contour_stack(zs) pyr_up();
translate([120, 0, 0]) fillet_contour_stack(zs) pocket();
