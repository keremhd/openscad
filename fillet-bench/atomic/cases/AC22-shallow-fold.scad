// atomic-case:      AC22
// atomic-title:     shallow 30 degree fold, NOT selected at the stock threshold
// atomic-family:    selection
// atomic-class:      capTri
// atomic-select:    both
// atomic-camera:    30,10,18.038 | 0.35,-1,0.6 | 46
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   capTri=8 weld=40 *=0
// atomic-expect:    fn=*  op=chamfer  fan=8 weld=40 *=0
// atomic-tier:      fn=*  op=*        tier=pullIn
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   shallow_crease at bench scale. The chevron's ridge turns 30 degrees, below
//                   the 46 degree selection threshold, so at stock it stays sharp and the
//                   plate's own 90 degree ends are all that blend. AC22 and AC22b are the
//                   two sides of one boundary and are only readable as a pair.
// atomic-history:   MEASURED CORRECTION: declared none, measures capTri=8 weld=40 -- the unselected
//                   fold contributes NO corner at all rather than a `none` corner, so the
//                   evidence that it was skipped is the ABSENCE of AC22b's tube=2, not a
//                   counter of its own. `none` has no case in this inventory and needs
//                   none: it tallies corners that produced nothing, and the catch-all
//                   already fails any case that grows one.
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
