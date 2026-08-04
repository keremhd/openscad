// Figure: min_angle= against the default 46-degree threshold.
//
// An edge counts as a feature when it turns by more than 1.5x the caller's own
// facet angle. At $fn = 24 that is 22.5 degrees, so a cylinder tessellated at
// the same $fn keeps its sides smooth: its seams turn 15 degrees and are read as
// tessellation, not as edges to round.
//
// Lower the threshold past that and the seams become features like any other.

$fn = 24;

module post() cylinder(r = 10, h = 14);

// Left: the default threshold. Only the two rims are rounded.
difference() {
    post();
    round_tool(r = 2) post();
}

// Right: min_angle = 10, below the 15-degree facet turn. Every vertical seam is
// now a feature too, and the post comes back fluted.
translate([30, 0, 0]) difference() {
    post();
    round_tool(r = 1, min_angle = 10) post();
}
