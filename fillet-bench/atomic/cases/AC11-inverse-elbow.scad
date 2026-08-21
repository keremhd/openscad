// atomic-case:      AC11
// atomic-title:     inverse elbow: two concave creases and one convex edge at one vertex
// atomic-family:    mixed-corner
// atomic-class:     tube
// atomic-select:    both
// atomic-camera:    12,6,12 | 1,1,1 | 58
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   tube=3 capTri=11 weld=26 *=0
// atomic-expect:    fn=*  op=chamfer  flat=3 fan=11 weld=26 *=0
// atomic-tier:      fn=*  op=fillet   tier=pullIn
// atomic-tier:      fn=*  op=chamfer  tier=fullMitre
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   step_notch at bench scale -- the L elbow read backwards. It is the case the
//                   plan flags as legitimately MIXED: tube and capTri at once, so it lands
//                   on both construction pages and its counter line is what makes the mix
//                   legible rather than implied.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    difference() {
        cube([24, 18, 18]);
        translate([12, 6, 12]) cube([13, 13, 7]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
