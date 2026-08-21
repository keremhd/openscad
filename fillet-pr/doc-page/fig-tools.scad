// Figure: the four tool nodes, each shown as the bare tool solid (back row) and
// composed with its target (front row).
//
// Left pair are the concave tools the caller unions; right pair the convex tools
// the caller subtracts.

$fn = 32;

module boss() {
    cube([26, 26, 6], center = true);
    translate([0, 0, 3]) cylinder(r = 6, h = 12);
}

module block() cube([18, 18, 12], center = true);

// --- back row: the tool solid on its own -----------------------------------

translate([0, 45, 0])   fillet_tool(r = 3) boss();
translate([40, 45, 0])  chamfer_tool(t = 3) boss();
translate([80, 45, 0])  round_tool(r = 3) block();
translate([115, 45, 0]) bevel_tool(t = 3) block();

// --- front row: composed with the target -----------------------------------

union() {
    boss();
    fillet_tool(r = 3) boss();
}

translate([40, 0, 0]) union() {
    boss();
    chamfer_tool(t = 3) boss();
}

translate([80, 0, 0]) difference() {
    block();
    round_tool(r = 3) block();
}

translate([115, 0, 0]) difference() {
    block();
    bevel_tool(t = 3) block();
}
