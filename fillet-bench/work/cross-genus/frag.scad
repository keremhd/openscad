// Views of the detached second component of cross.scad at r = 0.9.
// FMODE 0 the fragment alone; 1 the fragment plus the main solid clipped to a
// ball of radius BR about the fragment centroid; 2 the whole part with a marker.
FNSET = 0; $fn = FNSET;
R = 0.9;
FMODE = 0;
BR = 0.6;
CEN = [4.5797, -5.0608, -3.8475];

module solid() {
    fillet(r = R) union() {
        cylinder(d = 12, h = 40, center = true);
        rotate([0, 90, 0]) cylinder(d = 12, h = 40, center = true);
        rotate([90, 0, 0]) cylinder(d = 12, h = 40, center = true);
    }
}

if (FMODE == 0) import("frag09_1.stl");
else if (FMODE == 1) {
    import("frag09_1.stl");
    intersection() { import("frag09_0.stl"); translate(CEN) sphere(r = BR, $fn = 64); }
}
else if (FMODE == 2) {
    solid();
    translate(CEN) sphere(r = 2.0, $fn = 32);
}
