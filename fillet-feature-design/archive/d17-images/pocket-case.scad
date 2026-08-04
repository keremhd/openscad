// A concave pocket: a 40 x 40 x 20 block with a 20 x 20 x 12 recess.
// The pocket FLOOR is brushed with a slab 0.5 tall - well short of the radius 2 -
// so the four floor creases are selected and the four vertical inner corners
// are meant to stay sharp.
$fn = 32;
BX = 40; BY = 40; BZ = 20;
PX = 20; PY = 20; PD = 12;
R  = 2;
SLAB = 0.5;
FZ = BZ - PD;

module part() {
  difference() {
    cube([BX, BY, BZ]);
    translate([(BX-PX)/2, (BY-PY)/2, FZ]) cube([PX, PY, PD + 1]);
  }
}
module brush() translate([-1,-1,FZ-1]) cube([BX+2, BY+2, 1 + SLAB]);

module pocket() union() { part(); fillet_tool(r = R) { part(); brush(); } }
// Quadrant section: keep x<=20, y>=20 so the inner corner at (10,30) is exposed.
module section() intersection() { children(); translate([0,20,0]) cube([20.0001,20.0001,21]); }
