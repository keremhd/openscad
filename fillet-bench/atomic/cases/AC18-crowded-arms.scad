// atomic-case:      AC18
// atomic-title:     arms 6 thick against 2R = 5.8: crowded, and still legal
// atomic-family:    fit
// atomic-class:     tube
// atomic-select:    both
// atomic-camera:    6,0,6 | 1,-1,0.7 | 46
// atomic-params:    R=2.9 CT=2.9
//
// atomic-expect:    fn=*  op=fillet   tube=2 capTri=10 weld=16 *=0
// atomic-expect:    fn=*  op=chamfer  flat=2 fan=10 weld=16 *=0
// atomic-tier:      fn=*  op=fillet   tier=pullIn
// atomic-tier:      fn=*  op=chamfer  tier=fullMitre
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   elbow_crowded at bench scale. Measured NOT crowded out: the tangent lines all
//                   but collide and the exact corner is still built. R is 2.9 here rather
//                   than 2 because 2R against the 6 mm arm IS the case.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2.9;
CT = 2.9;
OP = 1;

module solid() {
    union() {
        cube([20, 14, 6]);
        cube([6, 14, 20]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
