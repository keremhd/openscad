// atomic-case:      AC15
// atomic-title:     the crease dies on a cylindrical end wall, three facets wide
// atomic-family:    curved
// atomic-class:      flat
// atomic-select:    both
// atomic-camera:    6,0.88,6 | 1,-1,0.7 | 50
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   flat=2 weld=2 *=0
// atomic-expect:    fn=8   op=fillet   cap=13 saddle=2 weld=4 *=0    the coarse row rebuilds the whole end wall out of pole caps
// atomic-expect:    fn=19  op=fillet   flat=2 fan=2 weld=11 *=0
// atomic-expect:    fn=64  op=fillet   capTri=2 fan=6 weld=136 *=0   the fine row falls off the ladder to baseline
// atomic-expect:    fn=*   op=chamfer  fan=2 weld=2 *=0
// atomic-expect:    fn=8   op=chamfer  flat=2 fan=2 weld=26 *=0
// atomic-expect:    fn=19  op=chamfer  flat=2 fan=2 weld=50 *=0
// atomic-expect:    fn=64  op=chamfer  flat=2 fan=2 weld=134 *=0
// atomic-mesh:      fn=*   op=fillet   VALID folds=0 warn=1   the curved end wall refuses creases at most rows
// atomic-mesh:      fn=64  op=fillet   VALID folds<=2 warn=0  the fine row blends every crease, and darts
// atomic-mesh:      fn=*   op=chamfer  VALID folds=0 warn=1
// atomic-mesh:      fn=8   op=chamfer  VALID folds=0 warn=0   the mitre tier takes every crease
// atomic-mesh:      fn=19  op=chamfer  VALID folds=0 warn=0
// atomic-mesh:      fn=64  op=chamfer  VALID folds<=2 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   elbow_curved_endface, re-framed and NOT re-scaled. r = 9.5 is load-bearing:
//                   at stock defaults it is exactly three facets across the terminus, and
//                   at r = 60 this model and elbow_facet_endface first rendered to identical
//                   meshes, which is how the trap was caught.
// atomic-history:   MEASURED CORRECTION: declared saddle, measures flat + fan at stock; saddle appears
//                   only at $fn = 8. Every row here warns.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    intersection() {
        union() {
            cube([20, 20, 6]);
            cube([6, 20, 24]);
        }
        translate([10, 9.5, -1]) cylinder(h = 60, r = 9.5);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
