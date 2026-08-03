// Attack 5: try to reach the one-segment exemption ON A CURVED WALL.
// A coarse cylinder (FN=16, R=40) whose facet seams (22.5 deg) are under the
// 33.75 deg threshold, so the wall reads as curved. TWO thin ribs leave the
// boss inside ONE facet, so the piece of base arc between them is a SINGLE mesh
// segment. A slab brush at plate level refuses the tall vertical rib/cylinder
// creases, so both ends of that one-segment arc are seam vertices whose arrival
// is a single segment -- the exemption arrivesStraight leaves open.
R=40; FN=16; RR=2; HRIB=8; SLAB=0.5; Y0=4.83; Y1=10.48; TWR=1;
$fn=FN; $fa=360/FN; $fs=0.01;
module part(){
  union(){
    translate([-70,-70,0]) cube([140,140,5]);
    translate([0,0,5]) cylinder(h=25, r=R);
    translate([20,Y0-TWR,5]) cube([40,TWR,HRIB]);
    translate([20,Y1,5]) cube([40,TWR,HRIB]);
  }
}
module brush() translate([-200,-200,4]) cube([400,400,1+SLAB]);
union(){ part(); fillet_tool(r=RR){ part(); brush(); } }
