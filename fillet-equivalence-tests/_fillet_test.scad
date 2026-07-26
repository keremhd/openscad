// -----------------------------------------------------------------------------
// M6-prep — automated equivalence harness (see ../detailed-milestones.md, M6)
//
// Proves two solids are the SAME shape to within a tolerance t, tessellation-
// independently. Used to check a fillet operator's output against the hand-
// written reference in ../fillet-visual-tests/_fillet_ref.scad.
//
// Idea (equivalence-by-emptiness): an exact symmetric difference is never empty,
// because the operator tessellates arcs differently from the reference. So we
// test two-sided containment after dilating each side by t:
//
//     (candidate - dilate(reference, t))  is empty   AND
//     (reference - dilate(candidate, t))  is empty
//
// <=> Hausdorff(candidate, reference) < t. Pick t above the arc chord error
// (~0.004*r at $fa=12) and below the smallest real defect worth catching.
//
// PASS condition is machine-detectable: when the union below is EMPTY, OpenSCAD
// prints "Current top level object is empty." and exits 1. check_equivalence.sh
// turns that into a passing test. A real error (wrong radius/shape, missing
// corner, gouge) exceeds t and leaves a shell -> non-empty -> failing test.
// -----------------------------------------------------------------------------

// L-infinity dilation by t (a cube "ball"): cheap, and enough to swallow the
// thin slivers between two tessellations of the same surface.
module fillet_dilate(t) minkowski() { children(); cube(2 * t, center = true); }

// children(0) = candidate (the operator tool), children(1) = reference tool.
// Renders empty iff the two agree to within t.
module fillet_equivalence(t) {
  union() {
    difference() { children(0); fillet_dilate(t) children(1); }
    difference() { children(1); fillet_dilate(t) children(0); }
  }
}
