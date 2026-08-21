// Figure: choosing which edges get treated, with the tool nodes and a brush.
//
// A block with a blind bore, its near half cut away so the inside shows. The bore floor is
// filleted, the bore mouth is rounded, and the block's own corners are left
// sharp - which the sign of an edge cannot express on its own, because the mouth
// and the corners are both convex.

$fn = 48;

module part() {
    difference() {
        cube([40, 40, 20]);
        translate([20, 20, 6]) cylinder(r = 8, h = 20);
    }
}

module cutaway() {
    difference() {
        children();
        translate([-1, -21, -1]) cube([42, 42, 22]);
    }
}

cutaway() difference() {
    union() {
        part();
        fillet_tool(r = 2) part();
    }
    round_tool(r = 2) {
        part();
        translate([20, 20, 14]) cylinder(r = 14, h = 8);
    }
}
