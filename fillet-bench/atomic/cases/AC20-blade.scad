// atomic-case:      AC20
// atomic-title:     a tilted box landing on a cube: refused creases beside built ones
// atomic-family:    refusal
// atomic-class:      capTri
// atomic-select:    both
// atomic-camera:    7.5,0,7.5 | 1,-1,0.6 | 60
// atomic-params:    R=0.5 CT=0.5
//
// atomic-expect:    fn=*   op=fillet   capTri=14 saddle=5 flat=1 weld=122 *=0
// atomic-expect:    fn=19  op=fillet   capTri=14 saddle=5 fan=1 weld=122 *=0
// atomic-expect:    fn=32  op=fillet   capTri=14 coons=1 saddle=2 flat=3 weld=122 *=0
// atomic-expect:    fn=64  op=fillet   capTri=14 saddle=2 flat=4 weld=122 *=0
// atomic-expect:    fn=*   op=chamfer  fan=20 weld=122 *=0
// atomic-tier:      fn=*   op=*        tier=baseline
// atomic-mesh:      fn=32  op=fillet   VALID folds=0 warn=1   the refusal band opens at the fine rows
// atomic-mesh:      fn=64  op=fillet   VALID folds=0 warn=1   the same
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   refused_neighbour, re-framed. All planar, so what it measures is the refusal
//                   path alone: one concave crease refused for size and four convex ones for
//                   crowding, while the cube's own top-front edge is built. The built bead
//                   runs into a vertex a refused crease also leaves, and it must not be left
//                   truncated and open (476ee531d).
// atomic-history:   MEASURED CORRECTION: declared flat, measures capTri + saddle with 122 welds; coons
//                   appears at 32 only.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 0.5;
CT = 0.5;
OP = 1;

module solid() {
    union() {
        cube(15, center = true);
        translate([0, 0, 10]) rotate([0, 80, 0]) translate([-4, -4, 0]) cube([8, 8, 10]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
