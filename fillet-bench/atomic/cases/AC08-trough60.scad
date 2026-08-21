// atomic-case:      AC08
// atomic-title:     60 degree trough, square end faces
// atomic-family:    mixed-corner
// atomic-class:     tube
// atomic-select:    both
// atomic-camera:    16,0,6 | -1,-1,0.9 | 74
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   capTri=12 flat=2 fan=2 weld=30 *=0
// atomic-expect:    fn=19  op=fillet   tube=2 capTri=10 cap=2 flat=1 fan=1 weld=26 *=0   the canal appears, and the size gate starts refusing
// atomic-expect:    fn=32  op=fillet   tube=2 capTri=10 cap=2 flat=1 fan=1 weld=26 *=0   the same
// atomic-expect:    fn=64  op=fillet   tube=2 capTri=10 cap=2 flat=1 fan=1 weld=26 *=0   the same
// atomic-expect:    fn=*   op=chamfer  flat=2 fan=10 weld=30 *=0
// atomic-tier:      fn=*   op=fillet   tier=baseline
// atomic-tier:      fn=19  op=fillet   tier=pullIn
// atomic-tier:      fn=32  op=fillet   tier=pullIn
// atomic-tier:      fn=64  op=fillet   tier=pullIn
// atomic-tier:      fn=*   op=chamfer  tier=fullMitre
// atomic-mesh:      fn=19  op=fillet   VALID folds=0 warn=1   the acute trough refuses some creases
// atomic-mesh:      fn=32  op=fillet   VALID folds=0 warn=1   the same
// atomic-mesh:      fn=64  op=fillet   VALID folds=0 warn=1   the same
// atomic-mesh:      fn=*   op=chamfer  VALID folds=0 warn=1   2 mm setbacks do not fit the 60 deg trough
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   elbow_dihedral60 at bench scale, written as an extruded polygon so the wall
//                   thickness is a true perpendicular 6 mm. Rotating a box instead buries
//                   its base inside the floor and adds edges that have nothing to do with
//                   this corner -- measured, and it is why the elbow family in this bench
//                   is polygonal wherever the wall is not vertical.
// mesh.py-comp:     1
//
// The wall leans up-LEFT out of the crease at x = 16, z = 6, so the interior angle
// between the floor top and the wall's inner face is 60 degrees. Both end faces are
// square to the crease, so the two ends are the same mixed corner twice.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    rotate([90, 0, 0]) translate([0, 0, -18]) linear_extrude(18)
        polygon([[0, 0], [26, 0], [26, 6], [22.93, 6],
                 [14.196, 21.12], [9, 18.12], [16, 6], [0, 6]]);
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
