// MIXED model, planar variant. bars.scad's two bars meeting at TH (whose
// meeting vertex is a brushed seam vertex arriving BENT, so withheld) PLUS a
// walled square pocket on the same plate whose four floor corners are SERVED.
// One slab brush at plate level covers both. MIXED=0 drops the pocket.
RR=2; TH=90; TW=4; FN=64; SLAB=0.5; PT=4; MIXED=1;
$fn=FN; $fa=360/FN; $fs=0.01;
module bar(a) rotate([0,0,a]) translate([0,-TW/2,PT]) cube([30,TW,12]);
module withheld(){
  union(){
    translate([-40,-40,0]) cube([80,80,PT]);
    bar(0);
    bar(TH);
  }
}
module served(){
  difference(){
    translate([-38,-38,PT]) cube([18,18,12]);
    translate([-34,-34,PT]) cube([10,10,13]);
  }
}
module part(){ if (MIXED) union(){ withheld(); served(); } else withheld(); }
module brush(){ translate([-60,-60,-1]) cube([200,200,PT+SLAB+1]); }
union(){ part(); fillet_tool(r=RR){ part(); brush(); } }
