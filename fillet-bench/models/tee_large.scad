// d = 40, above the 19 mm diameter where $fa takes over from $fs. The same
// shape as tee.scad on the other side of the binding term.
FNSET = 0; $fn = FNSET;
fillet(r = 3) union() {
    cylinder(d = 40, h = 60);
    translate([0, 0, 30]) rotate([0, 90, 0]) cylinder(d = 40, h = 40);
}
