// A D-shaped boss on a plate: base crease = half-circle arc + straight chord.
// The two vertices where arc meets chord carry a TALL sharp vertical crease that
// the brush excludes, so they are genuine BRUSHED seam vertices, and the base
// crease arrives at them ON A CURVE.
RR=2; FN=64; R=8; H=12; SLAB=0.5; TW=0; S=1;
$fn=FN; $fa=360/FN; $fs=0.01;
module part(){
  union(){
    translate([-30*S,-30*S,0]) cube([60*S,60*S,5*S]);
    translate([0,0,5*S]) intersection(){ cylinder(h=H*S, r=R*S); translate([-R*S,0,-1]) cube([2*R*S,2*R*S,H*S+2]); }
  }
}
module brush() translate([-200*S,-200*S,5*S-1*S]) cube([400*S,400*S,(1+SLAB)*S]);
union(){ part(); fillet_tool(r=RR*S){ part(); brush(); } }
