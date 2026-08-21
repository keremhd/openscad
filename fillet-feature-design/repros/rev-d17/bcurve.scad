// The curved twin of opocket.scad: a plate, a cylindrical boss, and a TALL rib
// running out of the boss. The rib/cylinder vertical creases are 8 mm tall and
// fit r perfectly - they are left unfilleted by the BRUSH, a SLAB-thin slab at
// plate level, exactly as opocket.scad leaves its four vertical creases. So the
// two vertices where the rib meets the boss at plate level are genuine BRUSHED
// seam vertices, and the crease arriving at them is the tessellated base ARC.
RR=2; FN=64; R=8; HRIB=8; SLAB=0.5; S=1;
$fn=FN; $fa=360/FN; $fs=0.01;
module part(){
  union(){
    translate([-30*S,-30*S,0]) cube([60*S,60*S,5*S]);
    translate([0,0,5*S]) cylinder(h=15*S, r=R*S);
    translate([0,-3*S,5*S]) cube([22*S,6*S,HRIB*S]);
  }
}
module brush() translate([-200*S,-200*S,(5-1)*S]) cube([400*S,400*S,(1+SLAB)*S]);
union(){ part(); fillet_tool(r=RR*S){ part(); brush(); } }
