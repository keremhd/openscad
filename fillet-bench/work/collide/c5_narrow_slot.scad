// Collision probe 5: a 3 wide slot in a block. Every edge of the slot is in
// collision range at once -- two concave floor creases 3 apart facing each other,
// and above them the two convex mouth edges 3 apart facing outward, so the
// round-overs eat the slot's side walls from both ends.
// RSET sweeps 1/2 .. 5/2 (2R = 1 .. 5 against a 3 wide, 8 deep slot).
RSET = 1;
FNSET = 0; $fn = FNSET;
fillet(r = RSET) difference() {
    cube([30, 24, 16]);
    translate([13.5, -1, 8]) cube([3, 26, 9]);
}
