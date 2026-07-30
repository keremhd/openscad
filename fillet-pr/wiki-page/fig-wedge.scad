// Figure: the tool solid for one straight crease, written out by hand.
//
// Left    the wedge: a prism laid along the crease, its cross-section the two
//         setback points and the corner pushed a hair past each wall. That is
//         chamfer_tool(t = 4) and nothing else.
// Middle  the same prism with a cylinder of radius 4 taken out of it, which is
//         fillet_tool(r = 4): a surface tangent to both walls.
// Right   what fillet_tool(r = 4) actually builds, for comparison.
//
// The crease runs away from the viewer so the cross-section faces the camera.
// The overshoot `E` is drawn at 0.6 so it can be seen; the builder uses a hair.

$fn = 48;

T = 4;      // setback / radius
E = 0.6;    // overshoot past each wall, exaggerated
H = 18;     // length of the crease

// The L, as a profile: wall A is y = 6, wall B is x = 6, and the crease between
// them runs up the extrusion at (6, 6). The empty quadrant is x > 6, y > 6.
module ell2d() polygon([[0, 0], [20, 0], [20, 6], [6, 6], [6, 20], [0, 20]]);

// The wedge's cross-section: five points, and every one of them is placed by
// the setback T or the overshoot E.
module wedge2d()
    polygon([[6 + T, 6],          // TA, T along wall A
             [6,     6 + T],      // TB, T along wall B
             [6 - E, 6 + T],      // TB pushed past wall B
             [6 - E, 6 - E],      // the corner pushed past both
             [6 + T, 6 - E]]);    // TA pushed past wall A

module lay() rotate([90, 0, 0]) linear_extrude(height = H) children();

// Left: the wedge alone.
translate([0, 0, 0]) {
    color("silver") lay() ell2d();
    color("gold") lay() wedge2d();
}

// Middle: the wedge minus the ball's path along the crease - here a cylinder,
// because the crease is straight and the walls meet at the same angle all the
// way along it.
translate([28, 0, 0]) {
    color("silver") lay() ell2d();
    color("gold") difference() {
        lay() wedge2d();
        translate([6 + T, 1, 6 + T]) rotate([90, 0, 0]) cylinder(r = T, h = H + 2);
    }
}

// Right: the operator's own answer.
translate([56, 0, 0]) {
    color("silver") lay() ell2d();
    color("gold") fillet_tool(r = T) lay() ell2d();
}
