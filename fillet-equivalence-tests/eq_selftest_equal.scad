// Self-test (positive): candidate and reference are the SAME bead, differing
// only in tessellation ($fn 96 vs 48). The harness must report EMPTY -> PASS.
// This proves the equivalence check tolerates tessellation and is not trivially
// always-failing.

use <_fillet_test.scad>;
use <../fillet-visual-tests/_fillet_ref.scad>;

r = 6;
H = 40;
t = 0.02 * r;

module candidate() ref_fillet_edge_z(10, 10, r, H, $fn = 96);
module reference() ref_fillet_edge_z(10, 10, r, H, $fn = 48);

fillet_equivalence(t) { candidate(); reference(); }
