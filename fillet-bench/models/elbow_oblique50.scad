// Gate probe: the same rake pushed to 40 degrees -- crease meets the end face at
// 50 degrees, the worst-conditioned oblique elbow this bench carries.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) intersection() {
    union() {
        cube([40, 30, 6]);
        cube([6, 30, 40]);
    }
    rotate([0, 0, 40]) translate([-60, 0, -10]) cube([120, 120, 80]);
}
