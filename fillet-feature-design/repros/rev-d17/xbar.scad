// Cross-drilled bar: a round hole through a square bar. The two mouth curves are
// circles on flat faces; the bar's own long edges are sharp and unfilleted.
RR=1.5; FN=64; S=1;
$fn=FN; $fa=360/FN; $fs=0.01;
module part(){ difference(){ translate([-10*S,-6*S,-6*S]) cube([20*S,12*S,12*S]); rotate([90,0,0]) cylinder(h=40*S,r=4*S,center=true); } }
difference(){ part(); round_tool(r=RR*S) part(); }
