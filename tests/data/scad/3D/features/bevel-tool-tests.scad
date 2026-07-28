// bevel_tool() is the same wedge with both signs flipped: it follows the convex
// creases and the caller subtracts it, so the tool sticks out into the air along
// every outer edge of its child.
//
// Left: the tool alone. Middle: the cube it came from with the tool subtracted —
// all twelve edges cut, the faces untouched. Right: a cylinder, whose two rims
// are closed chains and whose sides are tessellation seams that must stay sharp.
//
// The CGAL-backend baseline for this file is deliberately its own, and shows the
// cut faces inverted: the tool overshoots each wall by a hair so it crosses the
// surface transversally, and converting those slivers to a Nef polyhedron trips
// a CGAL assertion that discards facets. The tool is the same either way; only
// CGAL's rendering of it differs.

$fn = 32;

module block() cube([16, 16, 10]);

translate([-30, 0, 0]) bevel_tool(t = 2) block();

difference() {
    block();
    bevel_tool(t = 2) block();
}

translate([40, 8, 0]) difference() {
    cylinder(r = 8, h = 12);
    bevel_tool(t = 2) cylinder(r = 8, h = 12);
}
