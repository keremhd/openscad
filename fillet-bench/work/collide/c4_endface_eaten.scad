// Collision probe 4: the end-face round-over eats the whole end face.
// The standing arm is 8 wide and stands 5 proud of the plate, so its end face is
// 8 x 5. At R = 5/2 the round-over of the arm's top edge consumes the face's
// whole height and there is no flat left for the corner patch to seat on.
// RSET sweeps 1 .. 3 (2R = 2 .. 6 against a 5 tall end face).
RSET = 1;
FNSET = 0; $fn = FNSET;
fillet(r = RSET) union() {
    cube([40, 24, 6]);
    cube([8, 24, 11]);
}
