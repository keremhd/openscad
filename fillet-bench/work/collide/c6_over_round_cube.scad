// Collision probe 6: the degenerate extreme. A cube 6 on a side asked for a
// round-over up to R = 4, so 2R = 8 exceeds every dimension of the body and
// every one of the twelve round-overs collides with the one facing it. There is
// no correct answer here -- the question is whether the code refuses or tears.
// RSET sweeps 1 .. 4 (2R = 2 .. 8 against a 6 cube).
RSET = 1;
FNSET = 0; $fn = FNSET;
fillet(r = RSET) cube([6, 6, 6]);
