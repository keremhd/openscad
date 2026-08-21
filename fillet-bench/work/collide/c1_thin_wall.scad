// Collision probe 1: round-overs meet THROUGH the arm.
// A standing wall 2 thick on a plate. The two top round-overs of the wall each
// eat R off its thickness, so at R = 1 they touch and past R = 1 they overlap.
// RSET sweeps 1/2 .. 2 (2R = 1 .. 4 against a 2 thick wall).
RSET = 1;
FNSET = 0; $fn = FNSET;
fillet(r = RSET) union() {
    cube([40, 20, 6]);
    cube([2, 20, 30]);
}
