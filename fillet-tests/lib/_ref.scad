// -----------------------------------------------------------------------------
// Hand-written reference tools.
//
// These are the known-good answers, built from the classic OpenSCAD idioms
// (corner square - quarter cylinder, annulus prism - torus, ...). They do NOT
// use fillet_tool/round_tool/chamfer_tool/bevel_tool, so they render with a
// stock OpenSCAD and stay a genuinely independent yardstick for the operator.
//
// Sign convention, same as the operator's: a CONCAVE tool (fillet/chamfer) is
// unioned with the model, a CONVEX tool (round/bevel) is subtracted from it.
// -----------------------------------------------------------------------------

// ---- single 90-degree edge running along Z -----------------------------------

// Concave fillet bead. Faces are x = cx (material x < cx) and y = cy (material
// y < cy); the reentrant quadrant opens toward +x,+y. UNION with the model.
// Cross-section = corner square minus a quarter disc.
module ref_fillet_edge_z(cx, cy, r, h, z0 = 0) {
  translate([0, 0, z0])
    difference() {
      translate([cx, cy, 0]) cube([r, r, h]);
      translate([cx + r, cy + r, -1]) cylinder(r = r, h = h + 2);
    }
}

// Convex round tool. Convex corner at (cx,cy), material x < cx & y < cy.
// SUBTRACT from the model; it is the sharp corner sliver outside the arc.
module ref_round_edge_z(cx, cy, r, h, z0 = 0) {
  translate([0, 0, z0])
    difference() {
      translate([cx - r, cy - r, 0]) cube([r, r, h]);
      translate([cx - r, cy - r, -1]) cylinder(r = r, h = h + 2);
    }
}

// Concave chamfer bead — the wedge alone, no arc. UNION with the model.
module ref_chamfer_edge_z(cx, cy, t, h, z0 = 0) {
  translate([0, 0, z0])
    linear_extrude(h)
      polygon([[cx, cy], [cx + t, cy], [cx, cy + t]]);
}

// Convex bevel tool — the flat wedge cut off a convex corner at (cx,cy),
// material x < cx & y < cy. SUBTRACT from the model.
module ref_bevel_edge_z(cx, cy, t, h, z0 = 0) {
  translate([0, 0, z0])
    linear_extrude(h)
      polygon([[cx, cy], [cx - t, cy], [cx, cy - t]]);
}

// ---- closed ring edges around the Z axis -------------------------------------

// Convex mouth of a through hole of radius rh whose face is at z = zf, material
// below the face and outside the hole. SUBTRACT from the model.
// Annulus prism rh..rh+r over z[zf-r,zf], minus the torus at rho=rh+r, z=zf-r.
module ref_round_hole_mouth(rh, zf, r) {
  translate([0, 0, zf - r])
    difference() {
      difference() {
        cylinder(r = rh + r, h = r);
        translate([0, 0, -1]) cylinder(r = rh, h = r + 2);
      }
      rotate_extrude() translate([rh + r, 0]) circle(r = r);
    }
}

// Concave base of a boss of radius rb standing on a face at z = zf, material
// inside the boss and below the face. UNION with the model.
module ref_fillet_boss_base(rb, zf, r) {
  translate([0, 0, zf])
    difference() {
      difference() {
        cylinder(r = rb + r, h = r);
        translate([0, 0, -1]) cylinder(r = rb, h = r + 2);
      }
      rotate_extrude() translate([rb + r, r]) circle(r = r);
    }
}
