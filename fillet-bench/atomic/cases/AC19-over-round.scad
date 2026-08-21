// atomic-case:      AC19
// atomic-title:     R past half the face: the size gate must refuse
// atomic-family:    refusal
// atomic-class:     flat
// atomic-select:    both
// atomic-camera:    6,0,6 | 1,-1,0.7 | 48
// atomic-params:    R=4 CT=4
//
// atomic-expect:    fn=*  op=fillet   flat=2 none=10 *=0
// atomic-expect:    fn=*  op=chamfer  fan=12 *=0
// atomic-tier:      fn=*  op=*        tier=pullIn
// atomic-mesh:      fn=*  op=fillet   VALID folds=0 warn=1   THE EXPECTED ANSWER: the size gate refuses
// atomic-mesh:      fn=*  op=chamfer  VALID folds=0 warn=1   the same
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   MEASURED: ten of the twelve flat caps are now `none`. The few creases the gate
//                   still takes run straight off the open sides of the plate, and a strip end
//                   whose section the end face has absorbed needs no patch. The refusal
//                   itself is unchanged -- it is the warning count, not the counter line,
//                   that this case is really watching.
// atomic-history:   AC06 with the radius doubled. The 6 mm plate cannot carry two 4 mm setbacks,
//                   so the size gate leaves those creases sharp and warns. The REFUSAL is
//                   the expected answer here, and the warning
//                   count on the atomic-mesh line is what declares it: a cell that goes
//                   quiet is a MESH DRIFT, in the direction nobody watches.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 4;
CT = 4;
OP = 1;

module solid() {
    union() {
        cube([16, 12, 6]);
        cube([6, 12, 16]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
