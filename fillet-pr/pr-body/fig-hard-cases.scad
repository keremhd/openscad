// Figure: the configurations the case suite was built to stress.
//
// Left    a three-face pocket, cut open: an apex where three concave walls meet,
//         which has no closed-form equal-radius blend and is compared against a
//         shrink-and-grow reference instead
// Middle  a sheared five-sided pyramid: an asymmetric high-valence corner, where
//         the junction yields several distinct seated balls rather than the
//         single one a symmetric cone collapses to
// Right   a rib on a plate: a vertex where two concave creases and one convex
//         one meet, so whichever tool runs hears about only some of the walls
//         bounding its corner

$fn = 24;

module pocket() {
    difference() {
        translate([-30, -30, 0]) cube([60, 60, 65]);
        cylinder(r1 = 30, r2 = 0, h = 60, $fn = 3);
    }
}

module skew_apex() {
    multmatrix([[1, 0, 0.35, 0], [0, 1, 0.2, 0], [0, 0, 1, 0]])
        cylinder(r1 = 26, r2 = 0, h = 52, $fn = 5);
}

module rib() {
    cube([56, 44, 9]);
    translate([6, 6, 0]) cube([9, 30, 24]);
}

// The pocket is a void inside the block, so the near half is cut away to show
// it. The cutting box has to be generous in every direction: each slant crease
// ends on the block's underside and its bead reaches a little past that, so a
// box starting at the block's own bottom face slices the end off each bead and
// leaves three crumbs floating below the picture. That is the cut's doing, not
// the tool's - the tool on its own is one piece, genus 0.
difference() {
    union() {
        pocket();
        fillet_tool(r = 6) pocket();
    }
    translate([-40, -40, -20]) cube([80, 40, 100]);
}

translate([80, 0, 0]) difference() {
    skew_apex();
    round_tool(r = 3) skew_apex();
}

translate([135, -22, 0]) union() {
    rib();
    fillet_tool(r = 3) rib();
}
