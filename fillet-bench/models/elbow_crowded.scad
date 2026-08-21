// Gate probe: arms 6 thick against 2R = 5.8, so the two round-overs' tangent
// lines all but collide at the corner. The crowding boundary, still legal.
FNSET = 0; $fn = FNSET;
R = 2.9;
fillet(r = R) union() {
    cube([30, 20, 6]);
    cube([6, 20, 30]);
}
