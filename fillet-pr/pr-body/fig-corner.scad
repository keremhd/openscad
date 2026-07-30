// Figure: a corner, inside and out, and the ball that decides it.
//
// 1  an inside corner - three walls meeting at 90 degrees - with the ball seated
//    against all three at once
// 2  fillet_tool(r = 4) on it: three beads and the corner cell between them,
//    which is where the ball sits
// 3  the outside corner of a cube, cut open through the ball's centre. The ball
//    at an outside corner is inside the material, so a section is the only way
//    to see it seated against all three faces.
// 4  round_tool(r = 4) on the same cube, cut on the same plane: the tool is the
//    material outside the ball
//
// silver  the model        gold  the tool solid        red  the seated ball

$fn = 48;

R = 4;
S = 20;

// The section plane for the outside corner: y = R, through the ball's centre.
// The model is cut a hair deeper than the ball so the two faces do not land in
// the same plane.
module cutModel() intersection() {
    children();
    translate([-S, R + 0.05, -S]) cube([3 * S, 3 * S, 3 * S]);
}
module cutBall() intersection() {
    children();
    translate([-S, R, -S]) cube([3 * S, 3 * S, 3 * S]);
}

// An inside corner: the walls left behind are x = 6, y = 14 and z = 6.
module pocket() difference() {
    cube([S, S, S]);
    translate([6, -6, 6]) cube([S, S, S]);
}
module pocketBall() translate([6 + R, 14 - R, 6 + R]) sphere(r = R);

// An outside corner: the cube's own, the one facing the viewer at (S, 0, S).
module block() cube([S, S, S]);
module blockBall() translate([S - R, R, S - R]) sphere(r = R);

// 1: where the ball sits, inside corner - one radius off each of three walls.
translate([0, 0, 0]) {
    color("silver") pocket();
    color("tomato") pocketBall();
}

// 2: the tool that ball defines.
translate([28, 0, 0]) {
    color("silver") pocket();
    color("gold") fillet_tool(r = R) pocket();
}

// 3: where the ball sits, outside corner - one radius in from each of three
// faces, seen in section.
translate([56, 0, 0]) {
    color("silver") cutModel() block();
    color("tomato") cutBall() blockBall();
}

// 4: the whole tool for that cube, with the same corner left solid and the rest
// dropped to alpha - twelve beads and eight corner cells, one connected piece.
module nearCorner() translate([S, 0, S]) sphere(r = 2.4 * R);
translate([84, 0, 0]) {
    color("gold") intersection() {
        round_tool(r = R) block();
        nearCorner();
    }
    color([1, 0.84, 0, 0.18]) difference() {
        round_tool(r = R) block();
        nearCorner();
    }
}
