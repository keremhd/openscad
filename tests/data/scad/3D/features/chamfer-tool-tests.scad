// chamfer_tool() builds a concave tool solid — a flat wedge along every inner
// crease of its child — which the caller unions with the model.
//
// Left: the wedge on its own, so a change in the tool itself is visible rather
// than hidden under the model. Middle: the same wedge unioned into the L it came
// from. Right: a closed chain — the ring where a boss meets its plate — which
// wraps rather than ending, and so exercises the seam at the wrap point.

$fn = 32;

module ell() {
    cube([20, 6, 10]);
    cube([6, 20, 10]);
}

module boss() {
    cube([24, 24, 4], center = true);
    cylinder(r = 5, h = 10);
}

translate([-30, 0, 0]) chamfer_tool(t = 3) ell();

union() {
    ell();
    chamfer_tool(t = 3) ell();
}

translate([40, 12, 2]) union() {
    boss();
    chamfer_tool(t = 2) boss();
}
