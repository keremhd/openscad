// fillet_tool() builds a concave tool solid — a rounded bead along every inner
// crease of its child — which the caller unions with the model. It is the
// chamfer's wedge with the rolling ball taken back out of it, so the bead meets
// both walls tangentially instead of cutting the corner off flat.
//
// Left: the bead on its own, so a change in the tool itself is visible rather
// than hidden under the model. Middle: the same bead unioned into the L it came
// from. Right: a closed chain — the ring where a boss meets its plate — which
// wraps rather than ending, and so exercises the seam at the wrap point.
//
// Far right: the same L again with a selection brush as child 1, so the bead
// covers only the lower half of the crease and ends on a flat cap square to it.
// The cap is the point: clipping the spine gives a full cross-section there,
// where clipping the finished bead's swept ball would leave a scooped end.

$fn = 32;

module ell() {
    cube([20, 6, 10]);
    cube([6, 20, 10]);
}

module boss() {
    cube([24, 24, 4], center = true);
    cylinder(r = 5, h = 10);
}

translate([-30, 0, 0]) fillet_tool(r = 3) ell();

union() {
    ell();
    fillet_tool(r = 3) ell();
}

translate([40, 12, 2]) union() {
    boss();
    fillet_tool(r = 2) boss();
}

translate([70, 0, 0]) union() {
    ell();
    fillet_tool(r = 3) {
        ell();
        translate([-1, -1, -1]) cube([30, 30, 6]);
    }
}
