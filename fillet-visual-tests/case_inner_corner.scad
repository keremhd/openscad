// Case A — inner 90-degree corner (two cubes forming an L), edge along Z.
// Inner FILLET and inner CHAMFER, each at a small and a large radius.
//
// Read the rendered top-view (thin-slab section at z = 30) as a grid:
//   columns  ->  [ base model ] [ applied result ] [ isolated tool ]
//   rows     ->  fillet r=small, fillet r=large, chamfer t=small, chamfer t=large
// The large radius (35) deliberately exceeds the 30 mm face, so the bead pokes
// past the arm end — the §6.5 clamp/overflow case, made visible.

include <_fillet_ref.scad>;

$fn = 96;

H  = 60;    // extrusion height (edge length along Z)
dx = 100;   // column spacing (the "100 mm offset" for the isolated tool)
dy = 120;   // row spacing
CX = 10;    // inner corner x
CY = 10;    // inner corner y

r_small = 6;
r_large = 35;

module Lmodel(h = H) {
  cube([40, CY, h]);   // horizontal arm  x[0,40] y[0,10]
  cube([CX, 40, h]);   // vertical arm    x[0,10] y[0,40]
}

module tool(size, kind, h = H) {
  if (kind == "fillet")  ref_fillet_edge_z(CX, CY, size, h);
  if (kind == "chamfer") ref_chamfer_edge_z(CX, CY, size, h);
}

// one row: model | (model + tool) | tool
module row(size, kind) {
  Lmodel();
  translate([dx, 0, 0])     { Lmodel(); tool(size, kind); }
  translate([2 * dx, 0, 0]) tool(size, kind);
}

fillet_section(z = H / 2) {
  translate([0, 0 * dy, 0]) row(r_small, "fillet");
  translate([0, 1 * dy, 0]) row(r_large, "fillet");
  translate([0, 2 * dy, 0]) row(r_small, "chamfer");
  translate([0, 3 * dy, 0]) row(r_large, "chamfer");
}
