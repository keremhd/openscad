// A rib standing on a plate: two long concave creases and the ends where each
// runs out onto a face.
FNSET = 0; $fn = FNSET;
fillet(r = 1.5) union() {
    cube([50, 30, 5], center = true);
    translate([0, 0, 2.5]) cube([40, 5, 14], center = true);
}
