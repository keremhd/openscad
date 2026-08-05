FNSET = 0; $fn = FNSET;
module part() {
    union() {
        cylinder(d = 12, h = 40, center = true);
        rotate([0, 90, 0]) cylinder(d = 12, h = 40, center = true);
        rotate([90, 0, 0]) cylinder(d = 12, h = 40, center = true);
    }
}
union() { part(); fillet_tool(r = 2.0) part(); }
