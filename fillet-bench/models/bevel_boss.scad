// bevel_tool on a boss: the convex wedge, subtracted.
FNSET = 0; $fn = FNSET;
module part() {
    union() {
        cube([40, 40, 6], center = true);
        translate([0, 0, 3]) cylinder(d = 18, h = 12);
    }
}
difference() { part(); bevel_tool(t = 1.5) part(); }
