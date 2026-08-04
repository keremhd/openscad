// A brush selecting one edge of a box and leaving the rest sharp. Children 1+
// are the brush; a brush = argument is a parse error.
FNSET = 0; $fn = FNSET;
module part() { cube([40, 20, 12], center = true); }
union() {
    part();
    fillet_tool(r = 2) { part(); translate([0, 10, 6]) cube([44, 8, 8], center = true); }
}
