// CONTROL: a lone cylinder has two rims and no junction. Clean at every
// tessellation on record; if this moves, the instrument moved.
FNSET = 0; $fn = FNSET;
R = 1;
fillet(r = R) cylinder(d = 20, h = 30);
