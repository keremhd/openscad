// Collision probe 2: two parallel concave creases 4 apart, facing each other.
// Two walls on a plate leave a channel 4 wide; the floor fillet of each wall
// reaches R into the channel, so the two fillets meet at R = 2 and cross past it.
// RSET sweeps 1 .. 7/2 (2R = 2 .. 7 against a 4 wide channel).
RSET = 1;
FNSET = 0; $fn = FNSET;
fillet(r = RSET) union() {
    cube([40, 24, 6]);
    translate([8, 0, 6])  cube([6, 24, 20]);
    translate([18, 0, 6]) cube([6, 24, 20]);
}
