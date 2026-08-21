// atomic-case:      AC24
// atomic-title:     three arms at one vertex: more faces than an ear-clip can take
// atomic-family:    junction
// atomic-class:     fan
// atomic-select:    both
// atomic-camera:    6,6,6 | 1,1,1 | 52
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   tube=4 capTri=13 weld=15 *=0
// atomic-expect:    fn=19  op=fillet   tube=3 capTri=13 saddle=1 weld=15 *=0   one of the four canals gives way
// atomic-expect:    fn=32  op=fillet   tube=3 capTri=13 saddle=1 weld=15 *=0   the same
// atomic-expect:    fn=64  op=fillet   tube=3 capTri=13 saddle=1 weld=15 *=0   the same
// atomic-expect:    fn=*   op=chamfer  flat=4 fan=13 weld=15 *=0
// atomic-tier:      fn=*   op=fillet   tier=pullIn
// atomic-tier:      fn=*   op=chamfer  tier=fullMitre
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   NEW. With capTri, cap, coons and saddle all gated behind !isChamfer, the
//                   chamfer column has exactly two constructions left; this junction is
//                   where even the ear-clip cannot serve and the centroid fan is all that
//                   remains. The fillet column beside it is the control that shows what the
//                   ungated ladder does with the same vertex.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    union() {
        cube([20, 6, 6]);
        cube([6, 20, 6]);
        cube([6, 6, 20]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
