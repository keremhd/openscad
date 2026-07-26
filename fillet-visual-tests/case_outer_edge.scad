// Case C — outer vertical edge of a cube, a CONVEX 90-degree edge -> round_tool
// (the caller subtracts). Edge along Z.
//
// Top-view thin-slab section at z = 30:
//   columns -> [ base model ] [ applied (edge rounded) ] [ isolated tool ]
//   rows    -> round r=small, round r=large

include <_fillet_ref.scad>;

$fn = 96;

S  = 40;    // cube side
H  = 60;    // height along Z
dx = 100;   // column spacing
dy = 90;    // row spacing

r_small = 6;
r_large = 30;

module box() { cube([S, S, H]); }   // convex edge at (S,S) along Z

module tool(r) { ref_round_edge_z(S, S, r, H); }

module row(r) {
  box();
  translate([dx, 0, 0])     difference() { box(); tool(r); }   // applied
  translate([2 * dx, 0, 0]) tool(r);                          // isolated tool
}

fillet_section(z = H / 2) {
  translate([0, 0 * dy, 0]) row(r_small);
  translate([0, 1 * dy, 0]) row(r_large);
}
