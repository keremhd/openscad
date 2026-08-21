// atomic-case:      AC13
// atomic-title:     a coarse branch into a dense trunk: two facet scales at one junction
// atomic-family:    tessellation
// atomic-class:      flat
// atomic-select:    both
// atomic-camera:    0,0,10 | 1,-1,0.4 | 68
// atomic-params:    R=1.5 CT=1.5
//
// atomic-expect:    fn=*  op=fillet   flat=4 weld=144 *=0
// atomic-expect:    fn=*  op=chamfer  fan=4 weld=144 *=0
// atomic-tier:      fn=*  op=*        tier=pullIn
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   mixed_fn, re-framed and NOT re-scaled: the two cylinders carry the facet
//                   scales that are the whole point, and shrinking them would move both to
//                   the same one. R is 1.5 rather than 2 because a 12 mm trunk cannot take
//                   a 2 mm blend at the junction -- the local $fn on the two cylinders is
//                   likewise theirs and is not touched by the FNSET row.
// atomic-history:   MEASURED CORRECTION: declared fan, measures flat=4 with 144 welds -- the junction
//                   is ear-clipped and everything else on the two cylinders welds.
// mesh.py-comp:     1
//
// The $fn rows move the ARC of the blend here, not the two cylinders: their $fn is
// written on the primitives. That is deliberate -- it separates the operator's own
// tessellation axis from the source's.
FNSET = 0; $fn = FNSET;
R  = 1.5;
CT = 1.5;
OP = 1;

module solid() {
    union() {
        cylinder(d = 12, h = 20, $fn = 48);
        translate([0, 0, 10]) rotate([0, 90, 0]) cylinder(d = 12, h = 12, $fn = 10);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
