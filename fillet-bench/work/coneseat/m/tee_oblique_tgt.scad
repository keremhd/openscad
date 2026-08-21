// The same tee off the axis. An oblique arrival is where the size gate's misses
// and the curved-arrival fallback both live.
FNSET = 0; $fn = FNSET;
R = 1;
union() {
    cylinder(d = 10, h = 20);
    translate([0, 0, 10]) rotate([0, 80, 0]) cylinder(d = 10, h = 10);
}
