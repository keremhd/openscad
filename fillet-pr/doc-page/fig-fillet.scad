// Figure: fillet() and its two halves.
//
// A boss on a plate has something for each half to do: a closed concave ring
// where the cylinder meets the plate, and the plate's own convex outer edges.

$fn = 32;

module boss() {
    cube([26, 26, 6], center = true);
    cylinder(r = 5, h = 10);
}

                        boss();                              // untouched
translate([30, 0, 0])   fillet(r = 2) boss();                // both halves
translate([60, 0, 0])   fillet(r = 2, inner = false) boss();  // convex only
translate([90, 0, 0])   fillet(r = 2, outer = false) boss();  // concave only
