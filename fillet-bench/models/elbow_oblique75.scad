// Gate probe: square arms, but the crease dies into an end face raked 15 degrees
// off square -- the torus gate wants the crease perpendicular to that face.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) intersection() {
    union() {
        cube([40, 30, 6]);
        cube([6, 30, 40]);
    }
    rotate([0, 0, 15]) translate([-60, 0, -10]) cube([120, 120, 80]);
}
