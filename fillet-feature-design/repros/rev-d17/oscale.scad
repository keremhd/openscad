// opocket.scad with every length multiplied by S, and optionally rotated by ROT
// degrees about z (one facet of a 64-gon is 5.625 deg: rotating by exactly that
// is the same solid re-tessellated, and must measure the same).
TH=90; RR=2; FN=64; SLAB=0.5; L=24; D=12; FL=6; TW=5; S=1; ROT=0;
$fn=FN;
p = [[0,0],[L*S,0],[L*S+L*S*cos(TH), L*S*sin(TH)],[L*S*cos(TH), L*S*sin(TH)]];
module part(){ difference(){
  translate([0,0,-FL*S]) linear_extrude(height=(D+FL)*S) offset(delta=TW*S) polygon(p);
  linear_extrude(height=(D+1)*S) polygon(p); } }
module brush() translate([-200*S,-200*S,-1*S]) cube([400*S,400*S,(1+SLAB)*S]);
rotate([0,0,ROT]) union(){ part(); fillet_tool(r=RR*S){ part(); brush(); } }
