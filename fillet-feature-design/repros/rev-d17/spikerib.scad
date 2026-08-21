// A rib on a SPHERE. The rib's base creases ride the sphere and are curved.
RR=1.5; FN=48; S=1;
$fn=FN; $fa=360/FN; $fs=0.01;
module part(){ union(){ sphere(r=15*S); translate([-2*S,-2*S,0]) cube([4*S,4*S,22*S]); } }
union(){ part(); fillet_tool(r=RR*S) part(); }
