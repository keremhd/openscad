// The documented thin-slab idiom: a plate thin enough that both its faces are
// within reach of the same size.
FNSET = 0; $fn = FNSET;
R = 1;
fillet(r = R) cube([40, 40, 4], center = true);
