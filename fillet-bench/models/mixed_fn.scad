// CONTROL for promise 2, not a gate check and no longer the min_angle exhibit.
// Two cylinders tessellated at $fn = 48 and $fn = 10 in one solid: a mesh that
// honestly carries two facet scales at once. It is valid at stock defaults with
// no min_angle, which is what a constant threshold buys -- any rule that read
// the mesh would have to pick one of the two scales here and would fail on the
// other. What min_angle is for is now shown by shallow_crease.
FNSET = 0;
R = 1;
fillet(r = R) union() {
    cylinder(d = 12, h = 20, $fn = 48);
    translate([0, 0, 10]) rotate([0, 90, 0]) cylinder(d = 12, h = 12, $fn = 10);
}
