// Two overlapping bosses on a plate: their base rings cross at two points, so
// two closed curved chains meet at a junction.
module m() {
    cube([60, 40, 6]);
    translate([22, 20, 6]) cylinder(r = 10, h = 16);
    translate([38, 20, 6]) cylinder(r = 10, h = 16);
}
