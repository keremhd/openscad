// Collision probe 3: a convex round-over against a concave fillet across a
// 3 thick web. The plate is only 3 thick, so the wall's floor fillet (concave,
// on top of the plate) and the plate's own bottom-edge round-over (convex,
// underneath) reach for the same material once R passes 3/2.
// RSET sweeps 1 .. 3 (2R = 2 .. 6 against a 3 thick web).
RSET = 1;
FNSET = 0; $fn = FNSET;
fillet(r = RSET) union() {
    cube([40, 24, 3]);
    cube([8, 24, 26]);
}
