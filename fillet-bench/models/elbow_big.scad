// Control: lbracket at 3x the size with 3x the radius. The gate should fire here
// too -- a tolerance scaled to the model rather than to the radius would not.
FNSET = 0; $fn = FNSET;
R = 6;
fillet(r = R) union() {
    cube([120, 90, 18]);
    cube([18, 90, 120]);
}
