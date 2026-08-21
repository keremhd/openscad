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

// The section for the outside corner: the corner tip sliced off square to the
// body diagonal, through the ball's centre. The cut face is a triangle with one
// edge on each of the three faces the ball touches, which keeps the cube reading
// as a cube - a cut square to one face takes a whole face away with it. The
// circle is the ball's great circle; it is not tangent to the triangle's edges,
// because the three points where the ball touches the faces sit off this plane.
// `off` moves the plane along the diagonal.
NRM = [1, -1, 1] / sqrt(3);
CEN = [S - R, R, S - R];
BIG = 4 * S;
module slice(off) difference() {
    children();
    translate(CEN + off * NRM)
        rotate(a = 54.735610, v = [1, 1, 0])
            translate([-BIG / 2, -BIG / 2, 0]) cube([BIG, BIG, BIG]);
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
// faces. The corner tip is sliced off and the ball is left whole, so it stands
// out of the cut face as a ball rather than reading as a disc painted on it.
translate([56, 0, 0]) {
    color("silver") slice(0) block();
    color("tomato") blockBall();
}

// 4: the whole tool for that cube, with the same corner left solid and the rest
// dropped to alpha - twelve beads and eight corner cells, one connected piece.
// The corner left solid is the one furthest from the camera, so what shows is
// the cell's inner face - the surface the finished round actually leaves.
module farCorner() translate([0, S, 0]) sphere(r = 2.4 * R);
translate([84, 0, 0]) {
    color("gold") intersection() {
        round_tool(r = R) block();
        farCorner();
    }
    color([1, 0.84, 0, 0.18]) difference() {
        round_tool(r = R) block();
        farCorner();
    }
}
