// Case F — brush selecting only PART of an edge. Same inner-corner L as case A,
// edge along Z, but the fillet exists only for z in [0, brush_top]. The hand
// reference clips the bead (W) with a box brush, which gives the correct FLAT
// perpendicular cap (plan §7.2 — clip W, not U; no scooped round end).
//
// Contour stack at rising z: the upper sections (inside the brush) show the arc;
// the lower sections (past brush_top) are sharp again. The isolated-tool column
// shows the flat cap directly.

include <_fillet_ref.scad>;

$fn = 96;

H  = 60;
CX = 10;
CY = 10;
r  = 6;
brush_top = 30;     // fillet only where z < 30

module Lmodel() {
  cube([40, CY, H]);
  cube([CX, 40, H]);
}

module clipped_tool() {
  intersection() {
    ref_fillet_edge_z(CX, CY, r, H);
    translate([-10, -10, 0]) cube([200, 200, brush_top]);   // box brush: z < 30
  }
}

zs = [8, 22, 38, 52];   // two inside the brush, two past its cap

// applied (model + partial fillet)
fillet_contour_stack(zs) { Lmodel(); clipped_tool(); }

// isolated clipped tool — shows the flat perpendicular cap
translate([160, 0, 0]) fillet_contour_stack(zs) clipped_tool();
