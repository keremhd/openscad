// Case D — 3-face meeting point via a low-$fn cone ($fn = 3 -> triangular apex).
// Two forms:
//   left  column: the cone itself      -> CONVEX 3-face junction (round_tool)
//   right column: cube minus the cone  -> CONCAVE 3-face pocket   (fillet_tool)
//
// There is NO hand-written reference blend here on purpose: the equal-radius
// trihedral corner (plan §6.3) has no simple closed form — building it correctly
// is exactly what milestone M8 is for. This file exists to VISUALISE the section
// evolving toward the meeting point, so M8's output can later be dropped into the
// same contour stack and checked at a glance.
//
// Each column is a CONTOUR STACK: thin-slab sections at rising z, laid out along
// +Y, so one image shows the section collapse toward the apex.

include <_fillet_ref.scad>;

$fn = 3;            // 3-face apex

R  = 30;            // cone base radius
Hc = 60;           // cone height
zs = [6, 20, 34, 48, 56];   // section heights, marching toward the apex

module cone_up() { cylinder(r1 = R, r2 = 0, h = Hc); }   // convex apex at top

module pocket() {                                          // concave 3-face pit
  difference() {
    translate([-R, -R, 0]) cube([2 * R, 2 * R, Hc]);
    cone_up();
  }
}

// convex apex (round_tool target)
fillet_contour_stack(zs) cone_up();

// concave pocket (fillet_tool target)
translate([120, 0, 0]) fillet_contour_stack(zs) pocket();
