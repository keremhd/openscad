// Gate probe: planar end face, planar floor, but the standing trough wall is a
// coarsely faceted cylinder -- the concave crease is a broken arc, not one edge.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) union() {
    cube([40, 30, 6]);
    difference() {
        cube([16, 30, 40]);
        translate([66, 15, -1]) cylinder(h = 42, r = 60);
    }
}
