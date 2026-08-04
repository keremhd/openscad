// An internal pocket, rounded with round_tool subtracted from the block. All
// planar, so it is the one that does not depend on the classifier's threshold.
FNSET = 0; $fn = FNSET;
module part() {
    difference() {
        cube([40, 40, 16], center = true);
        translate([0, 0, 4]) cube([24, 24, 12], center = true);
    }
}
difference() { part(); round_tool(r = 2) part(); }
