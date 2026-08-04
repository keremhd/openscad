// Two overlapping bosses on a plate. Their shared seam meeting the plate is
// where D13's hole was; it is also a wiki figure and must be clean.
FNSET = 0; $fn = FNSET;
fillet(r = 1.5) union() {
    cube([50, 40, 6], center = true);
    translate([-6, 0, 3]) cylinder(d = 16, h = 12);
    translate([ 6, 0, 3]) cylinder(d = 16, h = 12);
}
