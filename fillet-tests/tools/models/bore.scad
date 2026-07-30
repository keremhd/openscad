// A blind bore in a block: a concave floor ring and a convex mouth ring, which
// the sign of an edge cannot tell apart from the block's own corners.
module m() {
    difference() {
        cube([40, 40, 20]);
        translate([20, 20, 6]) cylinder(r = 8, h = 20);
    }
}
