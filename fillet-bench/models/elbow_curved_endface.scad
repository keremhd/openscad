// Gate probe: the crease dies on a cylindrical end wall. The radius is 9.5 so
// that at stock defaults (12 deg facets, 2 mm chords) the crease's own end face
// is three facets wide -- a larger cylinder would put the whole corner inside one
// facet and quietly turn this back into a plain oblique elbow.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) intersection() {
    union() {
        cube([20, 20, 6]);
        cube([6, 20, 24]);
    }
    translate([10, 9.5, -1]) cylinder(h = 60, r = 9.5);
}
