// Figure: picking single edges and single corners out of a cube.
//
// A brush is a volume, not a list of edges: a crease is built where it lies
// inside the brush and left sharp where it does not, and it is CUT where the
// brush boundary crosses it, ending in a flat cap square to the crease.
//
// The three panels are the three answers that gives. Each treats the near
// vertical edge at (0, 0) and leaves the other eleven alone.

$fn = 32;

module block() cube([20, 20, 20]);

// 1. Part of one edge. The brush stops short of the top and bottom faces, so it
//    contains one vertical crease and no part of any other: 1 of 12 selected.
//    The round ends in a flat cap where the brush ends.
difference() {
    block();
    round_tool(r = 3) {
        block();
        translate([-4, -4, 4]) cube([8, 8, 12]);
    }
}

// 2. One whole edge, end to end. A brush tall enough to reach both faces also
//    contains the first millimetres of the four horizontal edges that meet this
//    one, and those get rounded too - 5 of 12 selected, not 1. Each of those
//    four overlaps runs into the corner at the shared vertex without covering
//    the radius down it, and a stretch that does that is not built. What decides
//    it is how far the brush reaches FROM the edge, against the radius: reach r
//    sideways and the four neighbours come too, so at r = 3 a box up to just
//    under 6 mm across gives one edge. This one is far narrower than it has to
//    be, to show that the width is free below that.
//
//    The brush's width does not shape the blend: the round here is the same
//    full r = 3 profile as the panel on the left, to six figures of volume per
//    millimetre. A brush says where, not how big.
translate([30, 0, 0]) difference() {
    block();
    round_tool(r = 3) {
        block();
        translate([-0.01, -0.01, -1]) cube([0.02, 0.02, 22]);
    }
}

// 3. One corner. A box around the vertex contains the three creases that meet
//    there, so the three beads and the corner cell between them are built and
//    the other nine edges stay sharp: 3 of 12 selected.
//
//    Note where the three beads stop: square across, at the brush wall. There is
//    no taper back into the sharp edge - a blend that fades out along a crease
//    is a runout, and this operator does not have one.
translate([60, 0, 0]) difference() {
    block();
    round_tool(r = 3) {
        block();
        translate([-2, -2, 10]) cube([12, 12, 12]);
    }
}
