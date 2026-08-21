// A branch pipe landing on a run pipe: the weld fillet is a closed curved crease
// whose angle changes at every point. The model that punishes anything which
// reasons about blend surfaces.
module m() {
    rotate([0, 90, 0]) cylinder(r = 10, h = 60, center = true);
    cylinder(r = 6, h = 16);
}
