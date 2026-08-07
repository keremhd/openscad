// The same step, with the inner corner blended by fillet_tool instead of by
// hand. Only for measuring the dihedral its output leaves where the blend
// meets the wall -- the number the hand-built profile is compared against.
FNSET = 48;
$fn = FNSET; $fa = 360/FNSET; $fs = 0.01;
R = 1.0; D = 1.0; W = 8; F = 12; T = 4; L = 20;
H = R + D;
module sharp() linear_extrude(height = L)
    polygon([[-W,-T],[F,-T],[F,0],[0,0],[0,H],[-W,H]]);
fillet(r = R, convex = false) sharp();
