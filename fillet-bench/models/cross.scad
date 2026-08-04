// Three cylinders on three axes through one point: junctions in every plane.
FNSET = 0; $fn = FNSET;
fillet(r = 1.2) union() {
    cylinder(d = 12, h = 40, center = true);
    rotate([0, 90, 0]) cylinder(d = 12, h = 40, center = true);
    rotate([90, 0, 0]) cylinder(d = 12, h = 40, center = true);
}
