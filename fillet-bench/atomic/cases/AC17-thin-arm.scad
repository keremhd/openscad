// atomic-case:      AC17
// atomic-title:     the standing arm is 3 thick against 2R = 4
// atomic-family:    fit
// atomic-class:     saddle
// atomic-select:    both
// atomic-camera:    3,0,6 | 1,-1,0.7 | 78
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   capTri=4 cap=2 saddle=2 weld=18 none=4 *=0
// atomic-expect:    fn=19  op=fillet   capTri=4 cap=2 coons=2 weld=18 none=4 *=0        the odd row reaches the Coons membrane
// atomic-expect:    fn=32  op=fillet   capTri=4 cap=2 coons=1 flat=1 weld=18 none=4 *=0 one of the two falls back to an ear clip
// atomic-expect:    fn=64  op=fillet   capTri=4 cap=2 flat=2 weld=18 none=4 *=0         and by 64 both do
// atomic-expect:    fn=*   op=chamfer  fan=10 weld=20 *=0
// atomic-tier:      fn=*   op=*        tier=pullIn
// atomic-mesh:      fn=*   op=fillet   VALID folds=0 warn=1   3 against 2R = 4: the round-overs do not both fit
// atomic-mesh:      fn=*   op=chamfer  VALID folds=0 warn=1   the same
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   elbow_thin_arm at bench scale. The two round-overs cannot both fit the face
//                   they are meant to share, so the turn takes the pull-in seat.
//                   The margin against AC06 is the only thing that tells the two cases
//                   apart: do not thin AC06 without re-reading this one.
// atomic-history:   MEASURED: four of the six flat caps this case used to close became `none`. They
//                   were the strip ends running off the two open sides; the end faces now
//                   take the section into their own outline, so those vertices need no
//                   patch at all. What is left under `flat` at 32 and 64 is a different
//                   thing entirely -- the Coons membrane falling back to an ear clip -- and
//                   keeping the two counted separately is what makes that ladder readable.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    union() {
        cube([24, 18, 6]);
        cube([3, 18, 20]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
