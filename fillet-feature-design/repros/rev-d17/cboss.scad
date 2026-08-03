RR=1; FN=64; R=25; SLAB=0.5; S=1;
$fn=FN; $fa=360/FN; $fs=0.01;
module part(){
  union(){
    rotate([-90,0,0]) cylinder(h=60*S, r=R*S, center=true);
    translate([-8*S,-20*S,0]) cube([16*S,40*S,(R+7)*S]);
  }
}
module brush() rotate([-90,0,0]) cylinder(h=100*S, r=(R+SLAB)*S, center=true);
union(){ part(); fillet_tool(r=RR*S){ part(); brush(); } }
