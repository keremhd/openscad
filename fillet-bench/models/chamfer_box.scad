// chamfer_tool on a plain box: the wedge tool with no curvature anywhere.
FNSET = 0; $fn = FNSET;
union() { cube([30, 20, 12], center = true); chamfer_tool(t = 2) cube([30, 20, 12], center = true); }
