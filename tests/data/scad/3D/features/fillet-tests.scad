// fillet() grows a circular-arc bead along every inner (concave) crease of its
// child and cuts a round along every outer (convex) one in a single pass;
// chamfer() does the same with a flat setback instead of an arc. The per-edge
// sign is read from the mesh and gated by convex=/concave=, so switching either
// off leaves the other half's edges untouched — the two operators are the whole
// public surface, and this file exercises each knob they take.
//
// A boss on a plate gives both halves something to do: a closed concave ring
// where the cylinder meets the plate, and the plate's own convex top edges. An L
// would not — its only inner crease is one short vertical edge, and a bead there
// is too small to tell concave=false from the default.
//
// Left to right: fillet default (both halves), fillet concave=false (the outer
// rounds only), fillet convex=false (the inner ring only), and chamfer default,
// so which knob did what is readable from the picture rather than from a count.

$fn = 32;

module boss() {
    cube([24, 24, 4], center = true);
    cylinder(r = 5, h = 10);
}

fillet(r = 2) boss();
translate([30, 0, 0]) fillet(r = 2, concave = false) boss();
translate([60, 0, 0]) fillet(r = 2, convex = false) boss();
translate([90, 0, 0]) chamfer(t = 2) boss();
// min_angle = 30 selects exactly what the derived default does on this model —
// every real crease here turns 90, and the cylinder's seams turn 11.25 — so it
// is here for the dump line rather than the picture, to catch the parameter
// being dropped on the floor. What a threshold does to a selection is pinned in
// the unit tests, where it is exact and milliseconds instead of a render and an
// eye.
translate([120, 0, 0]) fillet(r = 2, min_angle = 30) boss();
