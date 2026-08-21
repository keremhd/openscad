// atomic-case:      AC06
// atomic-title:     square elbow, the canonical mixed corner
// atomic-family:    mixed-corner
// atomic-class:     tube
// atomic-select:    both
// atomic-camera:    6,0,6 | 1,-1,0.7 | 48
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   tube=2 capTri=10 weld=16 *=0
// atomic-expect:    fn=*  op=chamfer  flat=2 fan=10 weld=16 *=0
// atomic-tier:      fn=*  op=fillet   tier=pullIn
// atomic-tier:      fn=*  op=chamfer  tier=fullMitre
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   before bc97e26eb this corner was the roll-field loft and the
//                   concave crease died into the end face through a creased V-notch.
//                   It is an exact torus patch now, and the reference every other
//                   elbow is read against. lbracket / elbow_big are this shape at 3x
//                   and 9x, which is why shrinking it is safe.
// mesh.py-comp:     1
//
// 16 x 12 x 6 with a 6-thick standing arm: lbracket's own 6-thick plate against
// R = 2, only shorter in its long extents. Shrinking the THICKNESS instead (the
// plan's "divide every extent by 3") puts the plate at 2 mm against 2R = 4 and
// the operator refuses -- measured, and it is AC19's case, not this one.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    cube([16, 12, 6]);
    cube([6, 12, 16]);
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
