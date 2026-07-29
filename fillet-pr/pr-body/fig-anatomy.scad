// Figure: what a tool solid is made of, along one crease.
//
// Left    chamfer_tool - the wedge: one prism cell per spine segment, its two
//         faces set back t along each wall
// Middle  fillet_tool  - the same wedge with the rolling ball taken back out of
//         it, so the surface left behind meets both walls tangentially
// Right   the ball itself, seated in the crease at three stations, which is what
//         gets subtracted

$fn = 48;

module ell() {
    cube([20, 6, 24]);
    cube([6, 20, 24]);
}

R = 4;

translate([0, 0, 0])  chamfer_tool(t = R) ell();
translate([30, 0, 0]) fillet_tool(r = R) ell();

// The seated ball: centre one radius off each wall, tangent to both.
translate([60, 0, 0]) {
    %ell();
    for (z = [4, 12, 20]) translate([6 + R, 6 + R, z]) sphere(r = R);
}
