// Figure: the tool solid for one straight crease.
//
// 1  chamfer_tool(t = 4) on an L - the wedge, a prism laid down the crease
//    reaching t along each wall
// 2  the same wedge, with the cylinder that is about to be taken out of it drawn
//    in red on the axis the ball rolls along
// 3  fillet_tool(r = 4) - what is left once that cylinder is gone
// 4  the cross-section with the overshoot drawn 150 times its size. Every cell
//    stands a hair past each wall so the tool crosses the surface instead of
//    resting on it, which makes the section a pentagon and not a triangle - but
//    that hair is 1e-3 of the size, 0.004 mm here, so panels 1-3 show a triangle
//    and are not wrong to.
//
// The model is trimmed back 0.05 at the near end, so what shows there is the
// tool's own end face rather than the model's resting in the same plane.
//
// silver  the model         gold  the tool solid        red  what gets subtracted

$fn = 48;

T = 4;      // setback / radius
H = 18;     // length of the crease
E = 1.5;    // overshoot, panel 4 only, drawn far larger than life - see below

// The L: wall A is y = 6, wall B is x = 6, and the crease between them runs
// along the extrusion at (6, 6).
module ell() rotate([90, 0, 0]) linear_extrude(height = H)
    polygon([[0, 0], [20, 0], [20, 6], [6, 6], [6, 20], [0, 20]]);

module trimmed() difference() {
    children();
    translate([-10, -0.05, -10]) cube([60, 10, 60]);
}

// The ball's axis: one radius off each wall, so a cylinder of radius T about it
// touches both walls all the way along. Drawn over the middle of the crease
// only, so the wedge it is about to be taken out of shows at both ends of it.
module roll() translate([6 + T, -(H - 8) / 2, 6 + T]) rotate([90, 0, 0]) cylinder(r = T, h = 8);

// 1: the wedge.
translate([0, 0, 0]) {
    color("silver") trimmed() ell();
    color("gold") chamfer_tool(t = T) ell();
}

// 2: the wedge and the cylinder that is coming out of it.
translate([28, 0, 0]) {
    color("silver") trimmed() ell();
    color("gold") chamfer_tool(t = T) ell();
    color("tomato") roll();
}

// 3: the rounded tool.
translate([56, 0, 0]) {
    color("silver") trimmed() ell();
    color("gold") fillet_tool(r = T) ell();
}

// 4: the cross-section itself, lying flat so it is seen face-on rather than
// down the crease - the L's profile in silver, the wedge's five points in gold.
// The two corners past the walls are what makes it a pentagon; at true size they
// are 0.004 mm and invisible, so they are drawn far larger than life here.
translate([92, -10, 0]) scale(1.8) {
    color("silver") linear_extrude(height = 1)
        polygon([[0, 0], [20, 0], [20, 6], [6, 6], [6, 20], [0, 20]]);
    color("gold") linear_extrude(height = 1.4)
        polygon([[6 + T, 6],          // TA, T along wall A
                 [6,     6 + T],      // TB, T along wall B
                 [6 - E, 6 + T],      // TB pushed past wall B
                 [6 - E, 6 - E],      // the corner pushed past both
                 [6 + T, 6 - E]]);    // TA pushed past wall A
}
