// atomic-case:      AC10b
// atomic-title:     the gate pushed harder: 55 deg rake on a 50 deg trough
// atomic-family:    mixed-corner
// atomic-class:     coons
// atomic-select:    both
// atomic-camera:    16,4.28,6 | -0.25,-1,1.0 | 78
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   tube=2 capTri=12 cap=1 flat=1 fan=1 weld=34 *=0
// atomic-expect:    fn=19  op=fillet   capTri=12 coons=2 fan=4 weld=31 *=0   THE COONS CELL: the odd row is where the membrane runs
// atomic-expect:    fn=32  op=fillet   tube=2 capTri=12 fan=3 weld=34 *=0
// atomic-expect:    fn=64  op=fillet   capTri=12 saddle=1 fan=5 weld=31 *=0
// atomic-expect:    fn=*   op=chamfer  fan=17 weld=37 *=0
// atomic-tier:      fn=*   op=fillet   tier=pullIn
// atomic-tier:      fn=19  op=fillet   tier=baseline
// atomic-tier:      fn=32  op=fillet   tier=pullIn
// atomic-tier:      fn=64  op=fillet   tier=baseline
// atomic-tier:      fn=*   op=chamfer  tier=baseline
// atomic-mesh:      fn=*   op=fillet   VALID folds<=1 warn=1   the 50 deg trough refuses creases at every row
// atomic-mesh:      fn=*   op=chamfer  VALID folds=0 warn=1
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   NEW, and the coverage insurance for coons. AC10 violates the gate two
//                   ways at once (a raked end face on a non-square trough) and STILL
//                   measures tube. This one pushes both violations further -- an acute 50
//                   degree trough under a 55 degree rake. If it too comes back tube, the
//                   COVERAGE GAP flag would fire on coons. It does not: at $fn = 19 this
//                   case measures coons=2, and it is the only mixed corner in the bench
//                   that does. The Coons membrane is an ODD-ROW phenomenon here -- AC12,
//                   AC17 and AC20 reach it at 19 and 32 as well and never at stock -- which
//                   is the parity axis showing up in dispatch rather than only in folds.
// mesh.py-comp:     1
//
// The wall leans up-LEFT out of the crease at x = 16, z = 6, so the trough is a 50
// degree dihedral. The rake plane is y = -x*tan(55) + 27.13; it crosses the crease at
// y = 4.28 (the camera centre) and leaves the near face at x = 19, inside the wall's
// own footprint -- a rake that instead exits within a millimetre of a face leaves a
// sliver the operator refuses. See AC10's history for what that cost.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    intersection() {
        rotate([90, 0, 0]) translate([0, 0, -34]) linear_extrude(34)
            polygon([[0, 0], [28, 0], [28, 6], [23.83, 6],
                     [10.95, 21.35], [6.355, 17.49], [16, 6], [0, 6]]);
        rotate([0, 0, -55]) translate([-70, 15.56, -10]) cube([160, 160, 50]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
