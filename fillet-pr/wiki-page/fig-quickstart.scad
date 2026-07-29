// Figure: the whole feature in one call.
//
// Left the model as written, right the same model wrapped in fillet(). A curved
// elbow rather than a square one, so both halves have work to do: the seams
// where the two cut ends meet the curved walls, and the four long outer edges.

$fn = 32;

module elbow() {
    rotate_extrude(angle = 90, $fn = 64) translate([10, 0]) square([10, 10]);
}

elbow();
translate([45, 0, 0]) fillet(r = 2) elbow();
