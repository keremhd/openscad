// atomic-case:      AC23
// atomic-title:     a rib running out of a cylindrical boss: the arriving crease is an arc
// atomic-family:    curved
// atomic-class:      cap
// atomic-select:    both
// atomic-camera:    7.42,-3,5 | 1,-1,0.7 | 62
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   cap=4 flat=2 weld=28 *=0
// atomic-expect:    fn=8   op=fillet   cap=8 fan=7 weld=30 *=0
// atomic-expect:    fn=19  op=fillet   cap=8 weld=46 *=0
// atomic-expect:    fn=32  op=fillet   cap=4 flat=2 weld=28 *=0
// atomic-expect:    fn=64  op=fillet   cap=7 flat=1 weld=84 *=0
// atomic-expect:    fn=*   op=chamfer  fan=2 weld=32 *=0
// atomic-expect:    fn=8   op=chamfer  weld=45 *=0
// atomic-expect:    fn=19  op=chamfer  weld=54 *=0
// atomic-expect:    fn=32  op=chamfer  fan=2 weld=32 *=0
// atomic-expect:    fn=64  op=chamfer  weld=95 *=0
// atomic-tier:      fn=*   op=*        tier=pullIn
// atomic-mesh:      fn=*   op=fillet   VALID folds=0 warn=1   the brushed boss refuses at the stock and 32 rows
// atomic-mesh:      fn=8   op=fillet   VALID folds=0 warn=0
// atomic-mesh:      fn=19  op=fillet   VALID folds=0 warn=0
// atomic-mesh:      fn=64  op=fillet   VALID folds<=1 warn=0   one dart at the curved arrival
// atomic-mesh:      fn=*   op=chamfer  VALID folds=0 warn=1
// atomic-mesh:      fn=8   op=chamfer  VALID folds=0 warn=0
// atomic-mesh:      fn=19  op=chamfer  VALID folds=0 warn=0
// atomic-mesh:      fn=64  op=chamfer  VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   rib_into_boss, re-framed and NOT re-scaled. The brush leaves the two vertical
//                   rib/boss creases sharp, so the vertices where the rib base crease meets
//                   the boss base arc are seam vertices with a CURVED arrival -- the only
//                   place the chainBulges() read could fire, and no other model in the bench
//                   reaches one.
// atomic-history:   MEASURED CORRECTION: declared flat, measures cap -- the pole fan, at four to eight
//                   corners depending on the row. This is the inventory's real cap case.
// mesh.py-comp:     1
//
// The slab brush sits at plate level: it covers the base creases and cuts the
// vertical ones off half a millimetre up, which is what leaves them unfilleted.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    union() {
        translate([-30, -30, 0]) cube([60, 60, 5]);
        translate([0, 0, 5]) cylinder(r = 8, h = 15);
        translate([0, -3, 5]) cube([22, 6, 8]);
    }
}
module brush() { translate([-200, -200, 4]) cube([400, 400, 1.5]); }

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   { solid(); brush(); }
else              chamfer(t = CT)  { solid(); brush(); }
