// atomic-case:      SFright
// atomic-title:     selftest fixture: the assertion layer must fire in both directions
// atomic-family:    selftest
// atomic-class:     flat
// atomic-select:    concave
// atomic-camera:    6,6,6 | 0.55,1,0.7 | 40
// atomic-params:    R=2 CT=2
// atomic-expect:    fn=*  op=fillet   flat=2 weld=1 *=0
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  fixture never-mind
// mesh.py-comp:     1
FNSET = 0; $fn = FNSET;
R = 2; CT = 2; OP = 1;
module solid() { cube([12, 12, 6]); translate([0, 0, 6]) cube([12, 6, 6]); }
if      (OP == 0) solid();
else if (OP == 1) fillet(r = R,   convex = false) solid();
else              chamfer(t = CT, convex = false) solid();
