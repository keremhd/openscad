// DOCUMENTATION CASE, NOT A GATE CHECK. Two cylinders tessellated differently in
// one solid. No single automatic threshold is right for both, and min_angle is
// the answer -- this is the model the manual should use to show why the argument
// exists. Expected to need min_angle; a bare render here is not a failure.
FNSET = 0;
fillet(r = 1) union() {
    cylinder(d = 12, h = 20, $fn = 48);
    translate([0, 0, 10]) rotate([0, 90, 0]) cylinder(d = 12, h = 12, $fn = 10);
}
