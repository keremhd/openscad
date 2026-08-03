// MIXED model, curved variant. bcurve.scad's geometry (a plate, a cylindrical
// boss and a tall rib, whose two brushed seam vertices arrive BENT on the
// tessellated base arc and are therefore withheld by the rule) PLUS a walled
// square pocket standing on the same plate, whose four floor corners are
// SERVED seam vertices (planar, single-segment arrivals). One slab brush at
// plate level covers both. MIXED=0 drops the pocket, leaving bcurve alone.
RR=2; FN=64; R=8; HRIB=8; SLAB=0.5; MIXED=1;
$fn=FN; $fa=360/FN; $fs=0.01;
module withheld(){
  union(){
    translate([-30,-30,0]) cube([60,60,5]);
    translate([0,0,5]) cylinder(h=15, r=R);
    translate([0,-3,5]) cube([22,6,HRIB]);
  }
}
module served(){
  difference(){
    translate([-28,-28,5]) cube([18,18,12]);
    translate([-24,-24,5]) cube([10,10,13]);
  }
}
module part(){ if (MIXED) union(){ withheld(); served(); } else withheld(); }
module brush() translate([-200,-200,4]) cube([400,400,1+SLAB]);
union(){ part(); fillet_tool(r=RR){ part(); brush(); } }
