// fillet() is the whole operator in one call: it grows a bead along every inner
// crease of its child and cuts a round along every outer one, in that order, so
// that the second pass sees the beads the first built. The tool nodes are the
// composable surface, and the comment above the node spells out how far a
// hand-written composition of them gets.
//
// A boss on a plate, because both halves have something obvious to do to it: a
// closed concave ring where the cylinder meets the plate, and the plate's own
// convex edges. An L would not do — its only inner crease is one short vertical
// edge, and a bead there is too small to tell inner = false from the default.
//
// Left: the default, both halves. Then inner= and outer= each switched off, so
// which half did what is readable from the picture rather than from a count.
//
// Far right is the same as the first, and looks it under a render — but not
// under a preview, which is the point of it. fillet() hands back its child
// untouched when $preview is set, so the preview baseline for this file is three
// bare bosses and one filleted one, and that fourth model is the only thing
// holding the opt-out honest. It is also the only invocation that survives into
// the .csg dump, since the dump runs the preview renderer too.
//
// The tool nodes never pass through: the other four *-tool-tests files look the
// same in preview as they do in render, deliberately.

$fn = 32;

module boss() {
    cube([24, 24, 4], center = true);
    cylinder(r = 5, h = 10);
}

fillet(r = 2) boss();
translate([30, 0, 0]) fillet(r = 2, inner = false) boss();
translate([60, 0, 0]) fillet(r = 2, outer = false) boss();
// min_angle = 30 selects exactly what the derived 18 does on this model — every
// real crease here turns 90, and the cylinder's seams turn 11.25 — so it is here
// for the dump line rather than the picture, to catch the parameter being
// dropped on the floor. What it does to a selection is pinned in the unit tests,
// where a threshold is milliseconds and exact instead of a render and an eye.
translate([90, 0, 0]) fillet(r = 2, min_angle = 30, disable_preview = false) boss();
