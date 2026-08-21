// atomic-case:      AC21
// atomic-title:     two near-coincident faces the boolean slivers
// atomic-family:    degenerate
// atomic-class:      capTri
// atomic-select:    both
// atomic-camera:    0.05,6,6 | -1,-0.7,0.7 | 56
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   capTri=10 saddle=1 fan=1 weld=14 *=0
// atomic-expect:    fn=19  op=fillet   capTri=8 weld=10 *=0   the sliver welds away and takes two corners with it
// atomic-expect:    fn=32  op=fillet   capTri=8 weld=10 *=0   the same
// atomic-expect:    fn=64  op=fillet   capTri=8 weld=10 *=0   the same
// atomic-expect:    fn=*   op=chamfer  fan=8 weld=10 *=0
// atomic-tier:      fn=*   op=fillet   tier=baseline
// atomic-tier:      fn=19  op=fillet   tier=pullIn
// atomic-tier:      fn=32  op=fillet   tier=pullIn
// atomic-tier:      fn=64  op=fillet   tier=pullIn
// atomic-tier:      fn=*   op=chamfer  tier=pullIn
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   the sliver weld / debris fuse (c31709f5d, 5fbc11f9a). The 0.05 mm step leaves
//                   a face far narrower than the two setbacks, so the crease pair either
//                   welds into one bead or is refused -- and either answer is legible only
//                   because this case is nothing else.
// atomic-history:   MEASURED CORRECTION: declared flat, measures capTri. The 0.05 mm step survives as
//                   a saddle plus a fan at stock and 8, and is WELDED AWAY from 19 up --
//                   two corners fewer and two creases fewer. That is the sliver fuse
//                   firing, and the row where it starts is the fact worth having.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    union() {
        cube([16, 12, 6]);
        translate([0.05, 0, 0]) cube([16, 12, 14]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
