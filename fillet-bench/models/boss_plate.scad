// A boss on a plate: one concave foot ring and one convex rim, the shape the
// fillet/round pair exists for.
FNSET = 0; $fn = FNSET;
fillet(r = 2) union() {
    cube([40, 40, 6], center = true);
    translate([0, 0, 3]) cylinder(d = 16, h = 14);
}
