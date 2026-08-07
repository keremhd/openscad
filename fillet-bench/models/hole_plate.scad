// A through hole in a plate: two convex rims on a curved wall, rounded.
FNSET = 0; $fn = FNSET;
R = 1.5;
module part() {
    difference() {
        cube([40, 40, 10], center = true);
        cylinder(d = 14, h = 30, center = true);
    }
}
fillet(r = R) part();
