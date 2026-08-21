// atomic-case:      AC22b
// atomic-title:     the same fold, selected: min_angle = 25
// atomic-family:    selection
// atomic-class:      tube
// atomic-select:    both
// atomic-camera:    30,10,18.038 | 0.35,-1,0.6 | 46
// atomic-params:    R=2 CT=2
// atomic-extra:     MINANG=25
//
// atomic-expect:    fn=*  op=fillet   tube=2 capTri=10 weld=40 *=0
// atomic-expect:    fn=*  op=chamfer  flat=2 fan=10 weld=40 *=0
// atomic-tier:      fn=*  op=fillet   tier=pullIn
// atomic-tier:      fn=*  op=chamfer  tier=fullMitre
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   shallow_crease at bench scale. The chevron's ridge turns 30 degrees, below
//                   the 46 degree selection threshold, so at stock it stays sharp and the
//                   plate's own 90 degree ends are all that blend. AC22 and AC22b are the
//                   two sides of one boundary and are only readable as a pair.
// mesh.py-comp:     1
//
// MINANG = -1 means "unset": FilletNode only forwards min_angle when it is >= 0,
// so the default row asks for the stock threshold honestly rather than by writing
// the threshold down here. AC22b passes -D MINANG=25 through atomic-extra.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

MINANG = -1;

module solid() {
    rotate([90, 0, 0]) translate([0, 0, -20]) linear_extrude(20)
        polygon([[0, 0], [30, 8.038], [60, 0], [60, 10], [30, 18.038], [0, 10]]);
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R, min_angle = MINANG)   solid();
else              chamfer(t = CT, min_angle = MINANG) solid();
