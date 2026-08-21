// Figure: selection brushes, on a block with a blind bore.
//
// The model has three kinds of feature and a brush is what tells them apart
// where the sign cannot:
//
//   bore floor ring   concave  -> fillet_tool   (the only concave crease: no brush)
//   bore mouth ring   convex   -> round_tool
//   the block's edges convex   -> round_tool, and NOT wanted
//
// The near half is cut away so the floor fillet inside the bore is visible.

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

// Left: no brush. round_tool finds every convex crease, so the block's own
// twelve edges are rounded along with the mouth.
cutaway() difference() {
    union() {
        part();
        fillet_tool(r = 2) part();
    }
    round_tool(r = 2) part();
}

// Right: a disc over the mouth as child 1 of round_tool. Children 1+ are
// selection brushes, unioned into one volume; only creases inside it are built.
translate([50, 0, 0]) cutaway() difference() {
    union() {
        part();
        fillet_tool(r = 2) part();
    }
    round_tool(r = 2) {
        part();
        translate([20, 20, 14]) cylinder(r = 14, h = 8);
    }
}
