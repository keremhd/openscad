// A tilted box landing on a cube, so that some creases of the junction fit the
// radius and their neighbours do not. All planar: no tessellation anywhere in
// the junction, so what it measures is the refusal path alone.
//
// The size gate refuses one concave crease at [7.5, 4, 7.44] (the blend leaves
// the surface it is meant to meet by 0.052) and four convex creases around
// [7.5, -4, 7.261] (crowding), while the cube's own top-front edge is built. The
// built bead therefore runs into a vertex a refused crease also leaves, which is
// A3's case: the refusal must not leave that bead truncated and open.
FNSET = 0; $fn = FNSET;
R = 0.5;
fillet(r = R, outer = false) union() {
    cube(15, center = true);
    translate([0, 0, 10]) rotate([0, 80, 0]) translate([-4, -4, 0]) cube([8, 8, 10]);
}
