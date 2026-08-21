// atomic-case:      AC07
// atomic-title:     square elbow with the end face raked 40 degrees
// atomic-family:    mixed-corner
// atomic-class:     tube
// atomic-select:    both
// atomic-camera:    6,6,6 | -1,-1,0.7 | 54
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   tube=2 capTri=10 weld=26 *=0
// atomic-expect:    fn=*  op=chamfer  flat=2 fan=10 weld=26 *=0
// atomic-tier:      fn=*  op=fillet   tier=pullIn
// atomic-tier:      fn=*  op=chamfer  tier=fullMitre
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   elbow_oblique50 at bench scale. It took the roll-field loft before the canal
//                   corner landed (75cb8e279) and is an exact canal now. The rake here is
//                   the mirror of that model's: the plane leaves the near face at x = 13.15,
//                   out on the open floor, so it cuts the crease without grazing the arm --
//                   a rake that exits within a millimetre of a face leaves a sliver the
//                   operator refuses, which cost three framings of AC10.
// mesh.py-comp:     1
//
// The crease is x = 6, z = 6. The rake plane is y = -x*tan(40) + 11.03, so the
// crease dies into it at y = 6: that is the camera centre, read off the source.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    intersection() {
        union() {
            cube([24, 18, 6]);
            cube([6, 18, 24]);
        }
        rotate([0, 0, -40]) translate([-60, 8.45, -10]) cube([140, 140, 50]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
