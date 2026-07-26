// -----------------------------------------------------------------------------
// M1 visual harness — shared reference tools + cross-cut viewer
//
// These are HAND-WRITTEN reference fillet/round/chamfer tools built from the
// classic OpenSCAD idioms (annulus - torus, wedge - cylinder, ...). They do NOT
// depend on the fillet_tool/round_tool nodes, so they render today and give the
// known-good target that later milestones (M5, M7) must reproduce.
//
// Convention for every case: geometry is built so the feature EDGE runs along Z,
// and we take a thin-slab horizontal cross-section (normal +Z). One image then
// shows, side by side along X:
//     [ base model ] [ applied result ] [ isolated tool solid ]
// and, stacked along Y, a small radius and a large radius (>= surface extent).
// -----------------------------------------------------------------------------

// Thin-slab section: intersect children with a thin horizontal slab at height z.
// XY size is deliberately huge so it covers the whole laid-out scene; only z and
// thickness matter for what the section shows.
module fillet_section(z = 0, thickness = 0.6, extent = 4000) {
  intersection() {
    children();
    translate([0, 0, z]) cube([extent, extent, thickness], center = true);
  }
}

// Vertical thin-slab section (normal +Y), for axis-symmetric ring features where
// a cut through the axis shows the rho-z profile and the ring fillet on both sides.
module fillet_vsection(y = 0, thickness = 0.6, extent = 4000) {
  intersection() {
    children();
    translate([0, y, 0]) cube([extent, thickness, extent], center = true);
  }
}

// Stack several thin-slab sections at different heights, translated apart along
// +Y so the section EVOLVING toward a junction is visible at a glance (used for
// the 3-/4-face meeting-point cases).
module fillet_contour_stack(zs, thickness = 0.6, spacing = 90, extent = 4000) {
  for (i = [0 : len(zs) - 1])
    translate([0, i * spacing, 0])
      fillet_section(z = zs[i], thickness = thickness, extent = extent)
        children();
}

// ---- reference tools for a single 90-degree edge running along Z -------------

// Inner (concave) fillet bead. Faces are x = cx (material x < cx) and y = cy
// (material y < cy); the reentrant quadrant opens toward +x,+y. Union this with
// the model. Cross-section = corner square minus a quarter disc (= a fillet).
module ref_fillet_edge_z(cx, cy, r, h, z0 = 0) {
  translate([0, 0, z0])
    difference() {
      translate([cx, cy, 0]) cube([r, r, h]);
      translate([cx + r, cy + r, -1]) cylinder(r = r, h = h + 2);
    }
}

// Outer (convex) round tool. Convex corner at (cx,cy), material x < cx & y < cy.
// SUBTRACT this from the model; it is the sharp corner sliver outside the arc.
module ref_round_edge_z(cx, cy, r, h, z0 = 0) {
  translate([0, 0, z0])
    difference() {
      translate([cx - r, cy - r, 0]) cube([r, r, h]);
      translate([cx - r, cy - r, -1]) cylinder(r = r, h = h + 2);
    }
}

// Inner (concave) chamfer bead — the wedge alone, no arc. Union with the model.
module ref_chamfer_edge_z(cx, cy, t, h, z0 = 0) {
  translate([0, 0, z0])
    linear_extrude(h)
      polygon([[cx, cy], [cx + t, cy], [cx, cy + t]]);
}
