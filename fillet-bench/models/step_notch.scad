// Gate probe: the inverse elbow -- a notched corner where two concave creases and
// one convex edge meet at one vertex, against the L elbow's two convex and one concave.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) difference() {
    cube([40, 30, 30]);
    translate([20, 10, 20]) cube([21, 21, 11]);
}
