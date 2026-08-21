// The two controls the standing rule asks for, in one file.
//   CASE = "pos"  a plain cube corner: round_tool MUST accept and round it
//   CASE = "neg"  two convex edges 0.1*R apart: round_tool MUST refuse
FNSET = 48;
$fn = FNSET; $fa = 360/FNSET; $fs = 0.01;
R = 1.0;
CASE = "pos";
GAP = 0.1;              // fin thickness, as a multiple of R, for CASE="neg"

module cubecorner() cube([10,10,10], center = true);
// A fin GAP*R thick standing on a slab: its two top edges are GAP*R apart and
// there is no room between them for two beads of radius R.
module fin() union() {
    translate([-6,-6,-2]) cube([12,12,2]);
    translate([-GAP*R/2,-6,0]) cube([GAP*R,12,4]);
}
if (CASE == "pos") difference() { cubecorner(); round_tool(r=R) cubecorner(); }
else difference() { fin(); round_tool(r=R) fin(); }
