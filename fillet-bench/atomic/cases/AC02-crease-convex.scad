// atomic-case:      AC02
// atomic-title:     one straight convex crease, both ends open
// atomic-family:    crease
// atomic-class:     none
// atomic-select:    both
// atomic-camera:    8,0,6 | 0.4,-1,0.8 | 40
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*  op=fillet   none=2 weld=1 *=0
// atomic-expect:    fn=*  op=chamfer  fan=2 weld=1 *=0
// atomic-tier:      fn=*  op=*        tier=pullIn
// atomic-mesh:      fn=*  op=*        VALID folds=0 warn=0
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   the convex twin of AC01. Per the mirror-features rule the two signs are the
//                   same solution, so this case exists to SHOW that, not to test a
//                   second algorithm: if AC01 and AC02 ever disagree on their counter
//                   line, one of the two signs has grown a special case.
// atomic-history:   MEASURED: the two signs DO disagree here, and the reason is the terminus
//                   bite rather than a special case in the operator. The convex round-over
//                   runs off both open ends of the crease, the end faces take the section
//                   into their own outline, and the two vertices that used to need a flat
//                   cap now need nothing at all -- none=2 where AC01 still measures flat=2.
//                   The concave crease's round-over stays inside the solid, so its ends are
//                   still closed by a cap. Same code, different arrival: this is the sign
//                   difference showing up in the boundary condition, not in the blend.
// mesh.py-comp:     1
//
// The brush -- children 1 and up of fillet() -- is the fourth isolation lever, and
// the one the sign filters cannot give here: convex = true on a plain box selects
// all twelve edges and eight trihedral corners, and no exact expectation could be
// written over them. The slab below covers exactly the top-front edge.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() { cube([16, 12, 6]); }
module brush()  { translate([-2, -2, 4]) cube([20, 4, 4]); }

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   { solid(); brush(); }
else              chamfer(t = CT)  { solid(); brush(); }
