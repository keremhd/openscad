// Figure: creases that are curves, not polylines.
//
// Left    a weld fillet where a branch pipe lands on a run pipe
// Middle  two overlapping bosses, whose base rings cross at two points
// Right   a hemispherical dome, a wall curved in both directions

$fn = 48;

module tee() {
    rotate([0, 90, 0]) cylinder(r = 10, h = 60, center = true);
    cylinder(r = 6, h = 16);
}

module bosses() {
    cube([60, 40, 6]);
    translate([22, 20, 6]) cylinder(r = 10, h = 16);
    translate([38, 20, 6]) cylinder(r = 10, h = 16);
}

module dome() {
    translate([-20, -20, 0]) cube([40, 40, 6]);
    translate([0, 0, 6]) sphere(r = 8);
}

translate([0, 0, 0]) union() {
    tee();
    fillet_tool(r = 2) tee();
}

translate([50, -20, 0]) union() {
    bosses();
    fillet_tool(r = 2) bosses();
}

translate([140, 0, 0]) union() {
    dome();
    fillet_tool(r = 2) dome();
}
