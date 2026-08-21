// atomic-case:      AC03
// atomic-title:     convex trihedral corner, three equal 90 degree arcs
// atomic-family:    corner
// atomic-class:     capTri
// atomic-select:    convex
// atomic-camera:    0,0,12 | -1,-1,0.8 | 46
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   capTri=8 weld=12 *=0
// atomic-expect:    fn=*  op=chamfer  fan=8 weld=12 *=0
// atomic-tier:      fn=*  op=*        tier=pullIn
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   the reference tri-cap. Three equal quarter arcs meeting at one vertex is the
//                   only shape capTri is allowed to close; AC05 rakes one face so that
//                   they are no longer equal and the pole fan has to take over.
// mesh.py-comp:     1
//
// convex = false would delete every corner here, so this case selects convex only:
// a plain cube's eight corners are eight copies of the same tri-cap, and the counter
// line is a complete statement about the call.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() { cube([12, 12, 12]); }

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R, convex = true, concave = false)   solid();
else              chamfer(t = CT, convex = true, concave = false) solid();
