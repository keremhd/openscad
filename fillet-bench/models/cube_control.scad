// CONTROL: a lone cube. Entirely planar, so no threshold derived from anything
// can reach it. Genus 0, always.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) cube([30, 30, 30], center = true);
