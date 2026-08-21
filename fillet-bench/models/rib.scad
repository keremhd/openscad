// A rib standing on a plate: two long concave creases and the ends where each
// runs out onto a face.
FNSET = 0; $fn = FNSET;
R = 1.5;
fillet(r = R) union() {
    cube([50, 30, 5], center = true);
    translate([0, 0, 2.5]) cube([40, 5, 14], center = true);
}
