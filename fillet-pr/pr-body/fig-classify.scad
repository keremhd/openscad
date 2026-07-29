// Figure: stage 1, classification. debug = true emits the diagnostic overlay
// instead of the tool solid - one marker per edge, coloured by what the
// classifier decided it was.
//
// Render this with F5. The overlay is a cloud of disjoint marker cubes rather
// than a solid, so it is for looking at, not for building with.

$fn = 24;

module part() {
    cube([40, 8, 20]);
    cube([40, 24, 8]);
    translate([32, 16, 8]) cylinder(r = 5, h = 10);
}

%part();
fillet_tool(r = 2, debug = true) part();
