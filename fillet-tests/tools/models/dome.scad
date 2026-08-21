// A hemisphere on a plate: a wall curved in both directions at once, which is
// what made the old off-face test refuse good blends by the sagitta r^2/2R.
module m() {
    translate([-20, -20, 0]) cube([40, 40, 6]);
    translate([0, 0, 6]) sphere(r = 8);
}
