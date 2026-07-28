// round_tool() is the same bead with both signs flipped: it follows the convex
// creases and the caller subtracts it, so the tool sticks out into the air along
// every outer edge of its child and leaves a rounded edge behind.
//
// Left: the tool alone. Middle: the cube it came from with the tool subtracted —
// all twelve edges rounded, the faces untouched.
//
// Right is the case the whole operator was specified against: rounding the mouth
// of a through hole has to give what a user would write by hand as an annulus
// prism with a torus taken out of it. Both rims of the hole are closed chains,
// and the bore's own tessellation seams have to stay sharp.

$fn = 32;

module block() cube([16, 16, 10]);

module plate() {
    difference() {
        translate([-12, -12, 0]) cube([24, 24, 8]);
        translate([0, 0, -1]) cylinder(r = 5, h = 10);
    }
}

translate([-30, 0, 0]) round_tool(r = 2) block();

difference() {
    block();
    round_tool(r = 2) block();
}

translate([44, 8, 0]) difference() {
    plate();
    round_tool(r = 2) plate();
}
