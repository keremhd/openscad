FNSET = 0; $fn = FNSET;
union() {
    cylinder(d = 12, h = 40, center = true);
    rotate([0, 90, 0]) cylinder(d = 12, h = 40, center = true);
    rotate([90, 0, 0]) cylinder(d = 12, h = 40, center = true);
}
