// bevel_tool on a boss: the convex wedge, subtracted.
FNSET = 0; $fn = FNSET;
R = 1.5;
module part() {
    union() {
        cube([40, 40, 6], center = true);
        translate([0, 0, 3]) cylinder(d = 18, h = 12);
    }
}
chamfer(t = R) part();
