// Three orthogonal cylinders through one point, at stock defaults. Minimal form
// of fillet-bench tile S1-T09 (models/cross.scad, h = 40).
//
// Symptom, measured 2026-08-04 at 00e7ec41f, --backend=manifold --render:
// the process is SIGKILLed after 89 s at a peak resident set of 17.5 GB, and no
// mesh is written. Run bare it reports exit 137; models/cross.scad at h = 40
// takes 194 s to reach the same end. Both ECHO lines and both fit warnings print first, so the log
// looks complete; the death comes later, in
//   buildRoundSolid -> dropVolumelessParts -> manifold::Manifold::Decompose.
//
// Add `min_angle = 19` to the fillet call and it completes in 0.2 s, VALID,
// chi = 2, genus 0, zero non-manifold edges. `min_angle = 18.9` is killed.
// The cliff sits exactly on 360/19 = 18.947 deg, the facet angle a d = 12
// cylinder achieves at stock $fa = 12 / $fs = 2, while the automatic threshold
// is 1.5*max($fa, 360/$fn) = 18 deg -- below it. So every facet seam reads as a
// crease: the mesh reports 59 surfaces instead of 3 and 374 convex creases
// instead of 114, and the tool is built from ~162 blend cells strung along
// tessellation seams. That component count is what Decompose runs out of
// memory materialising. This is D22's mechanism, one step past tee/tee_small:
// those survive as invalid solids, this one does not survive.
//
// h = 16 is invalid-but-finite (chi = 9, 13 non-manifold edges); h = 18 is the
// shortest arm found that kills the process.
fillet(r = 1.2) union() {
    cylinder(d = 12, h = 18, center = true);
    rotate([0, 90, 0]) cylinder(d = 12, h = 18, center = true);
    rotate([90, 0, 0]) cylinder(d = 12, h = 18, center = true);
}
