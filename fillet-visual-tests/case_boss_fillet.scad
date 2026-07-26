// Case B2 — base of a standing cylindrical boss, a CONCAVE ring edge ->
// fillet_tool (the caller unions). Closed-chain coverage, complementary to the
// convex hole mouth. Reference tool is again annulus_prism - torus (§1), added.
//
// Vertical thin-slab section through the axis (y = 0):
//   columns -> [ base model ] [ applied (base filleted) ] [ isolated tool ]
//   groups  -> small r (left), large r (right)

include <_fillet_ref.scad>;

$fn = 128;

W  = 80;    // plate width
T  = 15;    // plate thickness (top face at z = 0)
Rb = 12;    // boss radius
Hb = 40;    // boss height
dx = 100;   // column spacing

r_small = 5;
r_large = 24;

// plate top face at z = 0, boss standing on it up to z = Hb
module boss() {
  translate([0, 0, -T / 2]) cube([W, W, T], center = true);   // plate z[-T,0]
  cylinder(r = Rb, h = Hb);                                    // boss z[0,Hb]
}

// Concave base fillet tool (UNION): fills the reentrant corner at (Rb, 0).
// annulus prism Rb..Rb+r over z[0,r], minus the torus centred at rho=Rb+r, z=r.
module base_tool(r) {
  difference() {
    difference() {
      cylinder(r = Rb + r, h = r);
      translate([0, 0, -1]) cylinder(r = Rb, h = r + 2);
    }
    rotate_extrude() translate([Rb + r, r]) circle(r = r);
  }
}

module group(r) {
  boss();
  translate([dx, 0, 0])     { boss(); base_tool(r); }   // applied
  translate([2 * dx, 0, 0]) base_tool(r);              // isolated tool
}

// Radius groups stacked along +Z (both stay in the y = 0 cut plane) for a
// compact 3-column x 2-row grid.
dz = Hb + 20;

fillet_vsection(y = 0) {
  translate([0, 0,  0]) group(r_small);
  translate([0, 0, dz]) group(r_large);
}
