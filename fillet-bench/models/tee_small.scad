// d = 6, where $fs binds hardest at stock defaults: fragments come from
// 2*pi*r/$fs long before 360/$fa does.
FNSET = 0; $fn = FNSET;
fillet(r = 0.6) union() {
    cylinder(d = 6, h = 14);
    translate([0, 0, 7]) rotate([0, 90, 0]) cylinder(d = 6, h = 8);
}
