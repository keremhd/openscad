// Gate probe: a wall leaning 30 degrees off vertical -- a 120-degree trough on one
// end face and a 60-degree trough on the other, both elbows on the same face.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) translate([0, 30, 0]) rotate([90, 0, 0]) linear_extrude(30)
    polygon([[0, 0], [40, 0], [40, 6], [24, 6], [11, 28.52], [5.80, 25.52], [17.07, 6], [0, 6]]);
