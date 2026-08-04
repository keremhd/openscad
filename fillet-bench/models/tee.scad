// Two cylinders meeting at a tee. D22's own repro and the commonest shape in
// OpenSCAD: at stock defaults $fs binds and every wall seam clears the crease
// threshold, so the rims come back burred.
FNSET = 0; $fn = FNSET;
fillet(r = 1) union() {
    cylinder(d = 10, h = 20);
    translate([0, 0, 10]) rotate([0, 90, 0]) cylinder(d = 10, h = 10);
}
