// An L bracket: one reflex crease, and a convex chain running each end face's
// outline that turns 90 degrees at the reentrant corner. The turn is what D11
// was about; the bead running out onto the end face is what D12 is about. The
// profile is extruded along y so the default --viewall camera sees it.
module m() {
    cube([10, 30, 26]);
    cube([26, 30, 12]);
}
