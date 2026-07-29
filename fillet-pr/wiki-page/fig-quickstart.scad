// Figure: the whole feature in one call.
//
// Left the model as written, right the same model wrapped in fillet(). An L
// bracket, because it is the shape a reader reaches for first and because both
// halves of the operator have work to do on it: the reflex crease where the two
// slabs meet gets an inner bead added, and the outer edges of the outline get
// rounded away — including the reentrant corner of each end face, where the
// spine turns from one wall onto the next.
//
// The L profile is extruded along y so that the default --viewall camera, which
// looks at the y = 0 face, shows the profile rather than the back of a slab.

$fn = 32;

module bracket() {
    cube([10, 30, 26]);
    cube([26, 30, 12]);
}

bracket();
translate([36, 0, 0]) fillet(r = 2) bracket();
