// Figure: a crease made of many edges, inside and out. One ball per station,
// and the tool is the hull of each consecutive pair.
//
// 1  the foot of a boss - a concave crease running right round the cylinder -
//    with a ball seated at a dozen stations along it
// 2  fillet_tool(r = 3) on the same model: consecutive balls hulled into cells
// 3  the top rim - a convex crease round the same cylinder. The model is cut in
//    half and the balls left whole, so the seated balls on the near side stand
//    in the material they were removed from.
// 4  round_tool(r = 3) on it, cut the same way
//
// silver  the model        gold  the tool solid        red  the seated balls

$fn = 32;

R = 3;
PLATE = 30;
PH = 8;         // plate thickness
CR = 8;         // boss radius
CH = 14;        // boss height
NF = 12;        // stations drawn round the foot
NR = 8;         // and round the rim

module model() {
    cube([PLATE, PLATE, PH]);
    translate([PLATE / 2, PLATE / 2, 0]) cylinder(r = CR, h = PH + CH);
}

// A ball seated in a circular crease: one radius off the wall it stands beside
// and one off the floor or ceiling it stands on.
module ring(n, radius, z) for (i = [0 : n - 1]) rotate([0, 0, i * 360 / n])
    translate([radius, 0, z]) sphere(r = R);

module footBalls() translate([PLATE / 2, PLATE / 2, 0]) ring(NF, CR + R, PH + R);
module rimBalls()  translate([PLATE / 2, PLATE / 2, 0]) ring(NR, CR - R, PH + CH - R);

// The near half taken off, so what sits inside the material can be seen. The
// model is cut a hair behind the tool, so the two cut faces do not land in the
// same plane and the tool's cross-section reads as its own colour.
module cutAt(off) intersection() {
    children();
    translate([-PLATE, PLATE / 2 + off, -PLATE]) cube([3 * PLATE, 3 * PLATE, 3 * PLATE]);
}
module cut() cutAt(0.05) children();

// 1: where the balls sit, concave crease.
translate([0, 0, 0]) {
    color("silver") model();
    color("tomato") footBalls();
}

// 2: the tool they define.
translate([38, 0, 0]) {
    color("silver") model();
    color("gold") fillet_tool(r = R) model();
}

// 3: where the balls sit, convex crease, model cut away.
translate([76, 0, 0]) {
    color("silver") cut() model();
    color("tomato") rimBalls();
}

// 4: the tool they define. The model is drawn with the whole tool already taken
// out of it, so the gold sits in the void it removes rather than inside the grey,
// and its cut face stands a hair in front of the model's so the cross-section
// reads as its own colour.
translate([114, 0, 0]) {
    color("silver") difference() {
        cut() model();
        round_tool(r = R) model();
    }
    color("gold") cutAt(0) round_tool(r = R) model();
}
