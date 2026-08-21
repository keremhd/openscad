// atomic-case:      AC16
// atomic-title:     the end face kinks 10 degrees exactly at the crease
// atomic-family:    surface-grouping
// atomic-class:      coons
// atomic-select:    both
// atomic-camera:    6,0,6 | 1,-1,0.7 | 110
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   tube=1 capTri=10 coons=1 weld=55 *=0
// atomic-expect:    fn=*  op=chamfer  flat=2 fan=10 weld=55 *=0
// atomic-tier:      fn=*  op=*        tier=fullMitre
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   elbow_facet_endface, re-framed. The two thresholds are different numbers --
//                   about 10 degrees groups surfaces, 46 selects creases -- and this is the
//                   one place in the bench where they are seen apart: the shared face is two
//                   facets right at the grouping threshold rather than one plane.
// atomic-history:   RETIRED FINDING: the first atomic run stamped this case `noblend` -- the
//                   operator left 13 open edges and handed the model back unchanged at every
//                   row. That run was made against a transient mid-development binary that
//                   was never committed. On the committed binary the case blends cleanly at
//                   every row and the expectation above is the real dispatch: one tube and
//                   one Coons membrane at the kinked end, ten tri-caps elsewhere, and the
//                   whole build comes off the fullMitre tier with no warning. The `noblend`
//                   stamp was an artefact of the binary, not a regression in the operator.
// atomic-history:   This is the bench's clearest Coons case: the kink puts one corner between
//                   four boundary curves that no ear clip and no pole fan can close, and the
//                   membrane is what is left. AC17's odd rows reach it too, but only there.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    intersection() {
        union() {
            cube([40, 30, 6]);
            cube([6, 30, 40]);
        }
        translate([0, 0, -1]) linear_extrude(60)
            polygon([[-1, 0.6125], [6, 0], [41, 3.06], [41, 40], [-1, 40]]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
