// atomic-case:      AC04
// atomic-title:     concave trihedral corner, the mirror of AC03
// atomic-family:    corner
// atomic-class:     capTri
// atomic-select:    concave
// atomic-camera:    16,16,16 | 1,1,1 | 60
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   capTri=1 flat=3 weld=3 *=0
// atomic-expect:    fn=*  op=chamfer  fan=4 weld=3 *=0
// atomic-tier:      fn=*  op=*        tier=pullIn
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   the concave twin. Owner rule: convex and concave twins take the SAME code,
//                   so this case and AC03 must report the same construction. A run where
//                   one is capTri and the other is not is the finding, not the tolerance.
// mesh.py-comp:     1
//
// concave = ... , convex = false leaves only the notch's own inner vertex and the
// creases running into it.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    difference() {
        cube([18, 18, 18]);
        translate([8, 8, 8]) cube([11, 11, 11]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R, convex = false)   solid();
else              chamfer(t = CT, convex = false) solid();
