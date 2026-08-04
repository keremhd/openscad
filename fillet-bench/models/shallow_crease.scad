// DOCUMENTATION CASE, NOT A GATE CHECK. The model the manual uses to show why
// min_angle exists, replacing mixed_fn, which stopped exhibiting anything once
// the threshold became a constant.
//
// A chevron plate: two arms meeting at 15 degrees off straight, so the ridge
// along the fold turns 30 degrees -- convex on the top face, concave on the
// bottom. Every other crease on the plate turns 90. At the default 46-degree
// threshold the fold is not a feature and stays sharp while the ends and sides
// are blended; min_angle = 25 selects it and both folds come back rounded.
//
// Rear solid: stock defaults. Front solid: the same, min_angle = 25. The pair
// is the whole point of the tile, so both are rendered together. r = 4 rather
// than something smaller because a 30-degree crease is shallow: the blend reaches
// r/tan(15) = 14.9 mm along each face, and less than that is not visible in a
// contact-sheet tile, and the plate is 20 thick so that r = 4 fits between its
// own top and bottom creases rather than being refused for crowding.
FNSET = 0; $fn = FNSET;
R = 4;

// tan(15) * 60 = 16.077: the arm rise that puts the fold at 30 degrees.
CHEVRON = [[0, 0], [60, 16.077], [120, 0], [120, 20], [60, 36.077], [0, 20]];

module plate() {
    rotate([90, 0, 0]) linear_extrude(height = 40) polygon(CHEVRON);
}

fillet(r = R) plate();
translate([0, 55, 0]) fillet(r = R, min_angle = 25) plate();
