// Figure: the tool solid for one straight crease.
//
// Left    chamfer_tool(t = 4) on an L - the wedge, a prism laid down the crease
//         reaching t along each wall
// Middle  the same wedge, with the cylinder that is about to be taken out of it
//         drawn in red on the axis the ball rolls along
// Right   fillet_tool(r = 4) - what is left once that cylinder is gone
//
// silver  the model         gold  the tool solid        red  what gets subtracted

$fn = 48;

T = 4;      // setback / radius
H = 18;     // length of the crease

// The L: wall A is y = 6, wall B is x = 6, and the crease between them runs
// along the extrusion at (6, 6).
module ell() rotate([90, 0, 0]) linear_extrude(height = H)
    polygon([[0, 0], [20, 0], [20, 6], [6, 6], [6, 20], [0, 20]]);

// The ball's axis: one radius off each wall, so a cylinder of radius T about it
// touches both walls all the way along. Drawn short of the near end, so the
// wedge it is about to be taken out of is still visible in front of it.
module roll() translate([6 + T, -10, 6 + T]) rotate([90, 0, 0]) cylinder(r = T, h = H - 9);

// Left: the wedge.
translate([0, 0, 0]) {
    color("silver") ell();
    color("gold") chamfer_tool(t = T) ell();
}

// Middle: the wedge and the cylinder that is coming out of it.
translate([28, 0, 0]) {
    color("silver") ell();
    color("gold") chamfer_tool(t = T) ell();
    color("tomato") roll();
}

// Right: the rounded tool.
translate([56, 0, 0]) {
    color("silver") ell();
    color("gold") fillet_tool(r = T) ell();
}
