// chamfer_tool on a plain box: the wedge tool with no curvature anywhere.
FNSET = 0; $fn = FNSET;
R = 2;
chamfer(t = R) cube([30, 20, 12], center = true);
