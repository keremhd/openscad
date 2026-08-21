// atomic-case:      AC09
// atomic-title:     120 degree trough, leaning wall, square end faces
// atomic-family:    mixed-corner
// atomic-class:     tube
// atomic-select:    both
// atomic-camera:    6,0,6 | -1,-1,0.8 | 66
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   tube=4 capTri=12 weld=28 *=0
// atomic-expect:    fn=*  op=chamfer  flat=4 fan=12 weld=28 *=0
// atomic-tier:      fn=*  op=fillet   tier=pullIn
// atomic-tier:      fn=*  op=chamfer  tier=fullMitre
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   elbow_dihedral120 at bench scale. The obtuse twin of AC08: same construction,
//                   opposite side of the square dihedral the gate arithmetic assumes.
// mesh.py-comp:     1
//
// The wall leans up-RIGHT out of the crease at x = 6, z = 6 with the floor on its
// left, so the interior angle is 120 degrees.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    rotate([90, 0, 0]) translate([0, 0, -18]) linear_extrude(18)
        polygon([[0, 0], [24, 0], [24, 6], [12.93, 6],
                 [19.196, 16.86], [14, 19.86], [6, 6], [0, 6]]);
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
