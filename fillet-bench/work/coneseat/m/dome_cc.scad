// A hemisphere seated on a plate: a curved wall meeting a flat one all the way
// round, with no straight arrival anywhere on the crease.
FNSET = 0; $fn = FNSET;
R = 1.5;
fillet(r = R, outer = false) union() {
    cube([40, 40, 6], center = true);
    translate([0, 0, 3]) difference() {
        sphere(d = 24);
        translate([0, 0, -14]) cube([28, 28, 28], center = true);
    }
}
