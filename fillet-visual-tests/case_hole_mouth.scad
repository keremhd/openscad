// Case B — through-hole mouth (cube - cylinder), a CONVEX ring edge -> round_tool
// (the caller subtracts). Closed-chain coverage. Reference tool is the classic
// annulus_prism - torus idiom (../fillet-feature-design/fillet-operator-plan.md).
//
// Vertical thin-slab section through the axis (y = 0). Read as:
//   columns -> [ base model ] [ applied (mouth rounded) ] [ isolated tool ]
//   groups  -> small r (left), large r (right, reaches the plate edge)

include <_fillet_ref.scad>;

$fn = 128;

W  = 80;    // plate width
T  = 30;    // plate thickness (top face at z = T)
R  = 12;    // hole radius
dx = 100;   // column spacing

r_small = 5;
r_large = 24;   // 28 mm of face available; 24 rounds most of it

// plate centred on the Z axis, top face at z = T, hole radius R through it
module plate() {
  difference() {
    translate([0, 0, T / 2]) cube([W, W, T], center = true);
    translate([0, 0, -1]) cylinder(r = R, h = T + 2);
  }
}

// Convex mouth round tool (SUBTRACT): sharp corner sliver outside the arc.
// annulus prism R..R+r over z[T-r,T], minus the torus centred at rho=R+r, z=T-r.
module mouth_tool(r) {
  difference() {
    translate([0, 0, T - r])
      difference() {
        cylinder(r = R + r, h = r);
        translate([0, 0, -1]) cylinder(r = R, h = r + 2);
      }
    translate([0, 0, T - r]) rotate_extrude() translate([R + r, 0]) circle(r = r);
  }
}

module group(r) {
  plate();
  translate([dx, 0, 0])     difference() { plate(); mouth_tool(r); }   // applied
  translate([2 * dx, 0, 0]) mouth_tool(r);                            // isolated tool
}

// Radius groups stacked along +Z (both stay in the y = 0 cut plane), so the
// image is a compact 3-column x 2-row grid rather than a thin wide band.
dz = T + 40;

fillet_vsection(y = 0) {
  translate([0, 0,  0]) group(r_small);
  translate([0, 0, dz]) group(r_large);
}
