// A cylindrical boss on a square plate: one closed concave base ring, and the
// plate's own convex outline. The concave count here is the ring, one edge per
// cylinder facet, so it tracks $fn exactly -- worth remembering before reading a
// concave count as spurious features.
module m() {
    cube([26, 26, 6], center = true);
    cylinder(r = 5, h = 10);
}
