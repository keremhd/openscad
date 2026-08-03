// A served seam vertex carrying exactly ONE chain end, with the bead kept right
// out to that end. The brush is a thin strip along the y=0 bottom edge only, so
// that one edge is filleted over its whole length and the two perpendicular
// bottom edges get a stub shorter than r and are refused. Both ends of the bead
// then sit at a cube corner where an unfilleted crease leaves: served, ends=1.
RR = 1; FN = 96; HH = 0.5; STRIP = 0.001;
$fn = FN;
difference() {
  cube([20,20,20]);
  round_tool(r = RR) {
    cube([20,20,20]);
    translate([-1,-1,-1]) cube([22, 1+STRIP, HH+1]);
  }
}
