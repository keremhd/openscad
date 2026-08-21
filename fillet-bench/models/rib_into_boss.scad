// A rib running out of a cylindrical boss, brushed so the two vertical
// rib/boss creases stay sharp. The two vertices where the rib's base crease
// meets the boss's base arc are then seam vertices whose arriving crease is the
// tessellated base ARC -- a curved arrival. No other bench model reaches one:
// every seam vertex elsewhere is a corner of a planar crease.
FNSET = 0; $fn = FNSET;
R = 2;
module part() {
    union() {
        translate([-30, -30, 0]) cube([60, 60, 5]);
        translate([0, 0, 5]) cylinder(r = 8, h = 15);
        translate([0, -3, 5]) cube([22, 6, 8]);
    }
}
// A slab at plate level: it covers the base creases and cuts the vertical ones
// off a half-millimetre up, which is what leaves them unfilleted.
module brush() translate([-200, -200, 4]) cube([400, 400, 1.5]);
fillet(r = R) { part(); brush(); }
