FNSET = 0; $fn = FNSET;
fillet(r = 2) union() { cube([40, 40, 6], center = true); translate([0, 0, 3]) cylinder(d = 16, h = 14); }
