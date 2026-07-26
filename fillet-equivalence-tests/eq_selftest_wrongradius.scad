// Self-test (negative): candidate uses r=8, reference r=6. A genuine shape
// error far larger than t. The harness must report NON-EMPTY -> FAIL. This
// proves the check actually catches wrong geometry (i.e. it is not always PASS).

use <_fillet_test.scad>;
use <../fillet-visual-tests/_fillet_ref.scad>;

H = 40;
t = 0.02 * 6;

module candidate() ref_fillet_edge_z(10, 10, 8, H, $fn = 96);   // wrong radius
module reference() ref_fillet_edge_z(10, 10, 6, H, $fn = 96);

fillet_equivalence(t) { candidate(); reference(); }
