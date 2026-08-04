// A stepped box: the family where removing arrivesStraight measured worse, and
// the one whose fallback branch produces the fin.
FNSET = 0; $fn = FNSET;
fillet(r = 3) union() {
    cube([40, 40, 10]);
    translate([0, 0, 10]) cube([24, 40, 10]);
}
