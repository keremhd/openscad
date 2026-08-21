// atomic-case:      AC01
// atomic-title:     one straight concave crease, both ends open
// atomic-family:    crease
// atomic-class:     flat
// atomic-select:    concave
// atomic-camera:    6,6,6 | 0.55,1,0.7 | 40
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   flat=2 weld=1 *=0
// atomic-expect:    fn=*  op=chamfer  fan=2 weld=1 *=0
// atomic-tier:      fn=*  op=*        tier=pullIn
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   the simplest thing the operator does. If this tile is wrong,
//                   nothing further down the inventory is readable. The two flats
//                   are the strip ends against the open faces on the FILLET side; chamfer
//                   takes the same two ends as a centroid fan instead, because the ladder
//                   gates capTri, cap, coons and saddle behind !isChamfer. The weld is the
//                   pass-through where the two half-strips already sew.
// mesh.py-comp:     1
//
// convex = false is what makes this ONE feature: the same solid blended on both
// signs reports thirteen corners, twelve of which nobody is looking at -- and no
// exact expectation could be written over them.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    cube([12, 12, 6]);
    translate([0, 0, 6]) cube([12, 6, 6]);
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R,   convex = false) solid();
else              chamfer(t = CT, convex = false) solid();
