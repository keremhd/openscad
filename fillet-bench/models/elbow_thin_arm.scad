// Gate probe: the standing arm is 3 thick against 2R = 4 -- the round-overs
// cannot both fit on the face they are supposed to share.
FNSET = 0; $fn = FNSET;
R = 2;
fillet(r = R) union() {
    cube([40, 30, 6]);
    cube([3, 30, 30]);
}
