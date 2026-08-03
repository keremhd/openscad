// A rectangular pocket milled into a thick tube with a CYLINDRICAL floor.
// Floor is the cylinder r=RF (axis y). Its four boundary creases: two straight
// (the x=+-XW planes cut the cylinder in lines parallel to the axis) and two
// CURVED (the y=+-YW planes cut it in arcs). A cylindrical brush of radius
// RF+SLAB takes the floor creases whole and only a SLAB sliver of the four
// vertical corner creases, so those stay unfilleted: four brushed seam vertices.
RR=2; FN=64; R=25; RF=20; XW=10; YW=15; SLAB=0.5; S=1;
$fn=FN; $fa=360/FN; $fs=0.01;
module cutter(){
  intersection(){
    difference(){ rotate([-90,0,0]) cylinder(h=80*S,r=40*S,center=true);
                  rotate([-90,0,0]) cylinder(h=82*S,r=RF*S,center=true); }
    translate([-XW*S,-YW*S,0]) cube([2*XW*S,2*YW*S,60*S]);
  }
}
module part(){
  difference(){
    rotate([-90,0,0]) cylinder(h=60*S, r=R*S, center=true);
    rotate([-90,0,0]) cylinder(h=62*S, r=12*S, center=true);
    cutter();
  }
}
module brush() rotate([-90,0,0]) cylinder(h=100*S, r=(RF+SLAB)*S, center=true);
union(){ part(); fillet_tool(r=RR*S){ part(); brush(); } }
