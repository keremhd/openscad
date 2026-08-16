// Gate probe: end face square to the crease, but the trough opens 60 degrees
// instead of 90 -- the gate's swing arithmetic assumes the square dihedral.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) translate([0, 30, 0]) rotate([90, 0, 0]) linear_extrude(30)
    polygon([[0, 0], [40, 0], [40, 6], [6, 6], [23, 35.44], [17.80, 38.44], [0, 7.62]]);
