// atomic-case:      AC12
// atomic-title:     a rib running out onto the plate end face
// atomic-family:    crease-terminus
// atomic-class:      fan
// atomic-select:    both
// atomic-camera:    0,9,6 | -1,-0.8,0.7 | 42
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   capTri=12 fan=4 *=0
// atomic-expect:    fn=19  op=fillet   capTri=12 coons=2 fan=2 *=0   the odd row reaches the Coons membrane
// atomic-expect:    fn=32  op=fillet   capTri=12 saddle=2 fan=2 *=0  and the fine rows fall to the ring saddle
// atomic-expect:    fn=64  op=fillet   capTri=12 saddle=2 fan=2 *=0  the same
// atomic-expect:    fn=*   op=chamfer  fan=16 *=0
// atomic-tier:      fn=*   op=*        tier=unsplit
// atomic-mesh:      fn=32  op=fillet   VALID folds<=2 warn=0   two darts where the saddle meets the plate
// atomic-mesh:      fn=64  op=fillet   VALID folds<=2 warn=0   the same
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   rib / tee at bench scale, with the rib flush to the plate end so both of its
//                   base creases die ON a face rather than in mid-air. That terminus is the
//                   ear-clip, and it is the construction chamfer keeps when everything else
//                   is gated off behind !isChamfer.
// atomic-history:   MEASURED CORRECTION: declared flat, measures capTri + fan at stock, and the two
//                   rib termini are coons at 19 and saddle at 32 and 64. The ear-clip is
//                   not what a rib terminus takes here.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    union() {
        cube([26, 18, 6]);
        translate([0, 7, 6]) cube([14, 4, 10]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
