// Real case: the operator's concave tool for the inner 90-degree edge of an L
// (edge along Z) vs the hand-written reference bead.
//
// Status: RED until milestone M7. fillet_tool currently returns empty (M0 no-op),
// so (reference - dilate(candidate)) = reference, which is non-empty -> FAIL.
// When M7 makes fillet_tool produce the bead, this turns GREEN automatically.

use <_fillet_test.scad>;
use <../fillet-visual-tests/_fillet_ref.scad>;

$fn = 96;
H  = 40;
CX = 10;
CY = 10;
r  = 6;
t  = 0.02 * r;

module Lmodel() {
  cube([40, CY, H]);
  cube([CX, 40, H]);
}

// candidate: whatever the operator emits for the L's concave edge(s)
module candidate() fillet_tool(r = r) Lmodel();

// reference: the known-good bead for that one edge
module reference() ref_fillet_edge_z(CX, CY, r, H);

fillet_equivalence(t) { candidate(); reference(); }
