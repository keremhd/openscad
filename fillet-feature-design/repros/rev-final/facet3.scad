// Attack 5, second construction: FN=12, R=60 -> facet angle 30 deg, threshold
// 45 deg, so the wall reads curved; one facet spans a 31 mm chord. Two 5 mm
// ribs leave the boss inside that one facet with a 6 mm gap, so the arc between
// them is a SINGLE mesh segment and r=2 fits it. A slab brush refuses the tall
// vertical rib/cylinder creases, making both its ends seam vertices.
R=60; FN=12; RR=2; HRIB=8; SLAB=0.5; YA=2; YB=7; YC=13; YD=18;
$fn=FN; $fa=360/FN; $fs=0.01;
module part(){
  union(){
    translate([-100,-100,0]) cube([200,200,5]);
    translate([0,0,5]) cylinder(h=25, r=R);
    translate([30,YA,5]) cube([50,YB-YA,HRIB]);
    translate([30,YC,5]) cube([50,YD-YC,HRIB]);
  }
}
module brush() translate([-300,-300,4]) cube([600,600,1+SLAB]);
union(){ part(); fillet_tool(r=RR){ part(); brush(); } }
