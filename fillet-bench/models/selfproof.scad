// SELF-PROOF, not a gate check and not in expect.txt.
//
// The standing rule of this effort is that a new instrument must carry an
// invariant it can fail. This model is that invariant for sweep.sh: a solid
// rotated about z by exactly ROT facets is the same solid moved rigidly, so
// every topological count sweep.sh reports must be identical at ROT = 0 and
// ROT = 1. If they differ, the reader is reading the tessellation's placement
// rather than the mesh, and the sweep is not to be believed.
//
// It is a tee rather than a lone cylinder on purpose: a lone cylinder about z
// maps onto itself under a facet rotation, so it would pass vacuously. Here the
// crossbar is carried to a genuinely different place in space and the fillet
// runs over a junction, so the check has something to fail on.
FNSET = 0; $fn = FNSET;
R = 1;
ROT = 0;

rotate([0, 0, ROT * 360 / (FNSET > 0 ? FNSET : 360)])
fillet(r = R) union() {
    cylinder(d = 10, h = 20);
    translate([0, 0, 10]) rotate([0, 90, 0]) cylinder(d = 10, h = 10);
}
