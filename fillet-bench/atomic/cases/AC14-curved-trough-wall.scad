// atomic-case:      AC14
// atomic-title:     the trough wall is a coarsely faceted cylinder: the crease is a broken arc
// atomic-family:    curved
// atomic-class:     saddle
// atomic-select:    both
// atomic-camera:    7.91,0,6 | 1,-1,0.7 | 110
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   capTri=8 saddle=2 flat=2 fan=2 weld=44 *=0
// atomic-expect:    fn=8   op=fillet   tube=2 capTri=10 cap=1 fan=1 weld=54 *=0   the canal returns once the wall has facets to grip
// atomic-expect:    fn=19  op=fillet   tube=2 capTri=10 weld=54 *=0              clean canal
// atomic-expect:    fn=32  op=fillet   tube=2 capTri=8 flat=2 fan=9 weld=53 *=0  the 32 band frays: nine fans where 19 and 64 have none
// atomic-expect:    fn=64  op=fillet   tube=2 capTri=10 weld=56 *=0              clean canal again
// atomic-expect:    fn=*   op=chamfer  fan=12 weld=56 *=0
// atomic-expect:    fn=8   op=chamfer  flat=2 fan=10 weld=56 *=0
// atomic-expect:    fn=19  op=chamfer  flat=2 fan=10 weld=54 *=0
// atomic-expect:    fn=32  op=chamfer  flat=2 fan=19 weld=53 *=0
// atomic-tier:      fn=*   op=fillet   tier=pullIn
// atomic-mesh:      fn=*   op=fillet   VALID folds=0 warn=1   the stock row refuses: a 12 deg facet is the whole corner
// atomic-mesh:      fn=8   op=fillet   VALID folds=0 warn=0
// atomic-mesh:      fn=19  op=fillet   VALID folds=0 warn=0
// atomic-mesh:      fn=32  op=fillet   VALID folds=0 warn=0
// atomic-mesh:      fn=64  op=fillet   VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   elbow_curved_wall, re-framed and NOT re-scaled. A large-radius cylinder at
//                   stock defaults puts a whole corner inside one 12 degree facet and the
//                   model quietly becomes a plain oblique elbow; the bench measured this
//                   family as saddle at stock and tube at fn 16 and 64, which is the shift
//                   the ladder rows are here to show.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    union() {
        cube([40, 30, 6]);
        difference() {
            cube([16, 30, 40]);
            translate([66, 15, -1]) cylinder(h = 42, r = 60);
        }
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
