// An L bracket. All planar, one reflex crease, and the shape whose convex chain
// D11 turned out to be about.
FNSET = 0; $fn = FNSET;
R = 2;
union() {
    cube([40, 30, 6]);
    cube([6, 30, 40]);
}
