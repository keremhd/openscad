// Gate probe: square arms, planar end face -- except the end face kinks by 10
// degrees exactly at the crease, so the two round-overs' "shared face" is two
// facets right at the surface-grouping threshold rather than one plane.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) intersection() {
    union() {
        cube([40, 30, 6]);
        cube([6, 30, 40]);
    }
    translate([0, 0, -1]) linear_extrude(60)
        polygon([[-1, 0.6125], [6, 0], [41, 3.06], [41, 40], [-1, 40]]);
}
