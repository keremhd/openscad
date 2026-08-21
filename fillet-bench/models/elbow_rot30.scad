// Control: a plain square elbow, rotated 30 degrees about z and at a third the
// scale of lbracket. The gate should still fire -- nothing may assume axis alignment.
FNSET = 0; $fn = FNSET;
R = 1;
fillet(r = R) rotate([0, 0, 30]) union() {
    cube([20, 15, 3]);
    cube([3, 15, 20]);
}
