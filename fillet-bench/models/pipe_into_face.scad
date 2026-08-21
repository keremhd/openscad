// A cylinder plunging obliquely into a flat face -- D23's second family, where
// whole creases were discarded on misses that are not real.
FNSET = 0; $fn = FNSET;
R = 1;
fillet(r = R) union() {
    cube([40, 40, 10], center = true);
    rotate([0, 25, 0]) cylinder(d = 12, h = 24);
}
