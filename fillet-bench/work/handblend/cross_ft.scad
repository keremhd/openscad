FNSET = 0; $fn = FNSET;
R = 1.2;
union() {
  union() {
    cylinder(d = 12, h = 40, center = true);
    rotate([0, 90, 0]) cylinder(d = 12, h = 40, center = true);
    rotate([90, 0, 0]) cylinder(d = 12, h = 40, center = true);
  }
  fillet_tool(r = R) union() {
    cylinder(d = 12, h = 40, center = true);
    rotate([0, 90, 0]) cylinder(d = 12, h = 40, center = true);
    rotate([90, 0, 0]) cylinder(d = 12, h = 40, center = true);
  }
}
