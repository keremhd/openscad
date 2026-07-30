// Figure: the rounded blend as a ball, with no wedge in sight.
//
// 1  a ball of the requested radius seated in the crease: its centre is one
//    radius off each wall, so it touches both
// 2  what it sweeps when it slides down the crease - a cylinder of that radius
//    on the same axis, capped by the ball at each end
// 3  the finished blend - the model with that sweep taken out of the corner,
//    which is what fillet_tool(r = 4) builds
// 4  a corner, where three creases meet: here the sweep has no axis to run
//    along, and it is one ball seated against all three walls at once. That is
//    what a corner cell is hulled from.

$fn = 48;

R = 4;
H = 18;
Y0 = -2;        // the sweep runs from y = Y0 down to y = Y0 - L
L = 14;

module ell2d() polygon([[0, 0], [20, 0], [20, 6], [6, 6], [6, 20], [0, 20]]);
module ell() rotate([90, 0, 0]) linear_extrude(height = H) ell2d();

// Seated: one radius off wall A (y = 6) and one off wall B (x = 6). The centre
// runs along that line, so the swept solid is a cylinder about it.
module seated(y) translate([6 + R, y, 6 + R]) sphere(r = R);
module axis() translate([6 + R, Y0, 6 + R]) rotate([90, 0, 0]) cylinder(r = R, h = L);

// 1: the ball, seated.
translate([0, 0, 0]) {
    color("silver") ell();
    color("gold") seated(Y0 - L / 2);
}

// 2: the sweep - a cylinder between the two end balls.
translate([28, 0, 0]) {
    color("silver") ell();
    color("gold") { axis(); seated(Y0); seated(Y0 - L); }
}

// 3: the blend that sweep leaves behind.
translate([56, 0, 0]) color("silver") union() {
    ell();
    fillet_tool(r = R) ell();
}

// 4: a corner. The pocket's three walls are x = 6, y = 14 and z = 6, so the
// seated ball sits one radius off each of them.
translate([84, 0, 0]) {
    color("silver") difference() {
        cube([20, 20, 20]);
        translate([6, -6, 6]) cube([20, 20, 20]);
    }
    color("gold") translate([6 + R, 14 - R, 6 + R]) sphere(r = R);
}
