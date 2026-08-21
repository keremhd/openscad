// atomic-case:      AC05
// atomic-title:     convex corner whose three arcs are unequal: the top face is raked twice
// atomic-family:    corner
// atomic-class:      capTri
// atomic-select:    convex
// atomic-camera:    0,0,8 | -1,-1,0.9 | 46
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   capTri=8 weld=14 *=0
// atomic-expect:    fn=19  op=fillet   capTri=8 fan=1 weld=13 *=0    one pole starts fanning
// atomic-expect:    fn=32  op=fillet   capTri=8 fan=1 weld=13 *=0    the same
// atomic-expect:    fn=64  op=fillet   capTri=8 fan=2 weld=12 *=0    and a second one
// atomic-expect:    fn=*   op=chamfer  fan=8 weld=14 *=0
// atomic-tier:      fn=*   op=*        tier=pullIn
// atomic-mesh:      fn=32  op=fillet   VALID folds<=2 warn=0   fine-fn seam darts at the raked poles
// atomic-mesh:      fn=64  op=fillet   VALID folds<=2 warn=0   the same, and by 64 they close again
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   capTri needs three EQUAL arcs. Raking the top face about both x and y makes
//                   all three dihedrals at the corner different, which is what the pole fan
//                   exists for. If this case measures capTri instead, rake harder and say so
//                   here -- cap has no second case in the inventory.
// atomic-history:   MEASURED CORRECTION: raking the top face about both x and y did NOT move this
//                   corner off capTri -- all eight corners are capTri at every row, and
//                   only a fan or two appear at 19 and above. So three EQUAL arcs is not
//                   the tri-cap's real precondition. cap is reached by AC23 (a brushed
//                   boss, cap=4 to cap=8), AC17 and AC08 instead, so the plan's worry
//                   that cap had one thin case is closed from the other direction.
// mesh.py-comp:     1
//
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    difference() {
        cube([16, 16, 12]);
        rotate([12, -18, 0]) translate([-40, -40, 8]) cube([100, 100, 40]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R, convex = true, concave = false)   solid();
else              chamfer(t = CT, convex = true, concave = false) solid();
