// -----------------------------------------------------------------------------
// Shared harness: sectioning viewers, the six-column case row, and the
// emptiness-based checks that the shell drivers run headlessly.
//
// Every case file includes this plus _ref.scad and then defines the case
// contract described in ../README.md. Both drivers — render.sh (picture) and
// check.sh (pass/fail) — consume that one contract, so a case is written once.
//
// The last column of the picture is exactly the geometry the "tool" check
// evaluates: an empty red column and a passing test are the same fact.
// -----------------------------------------------------------------------------

// Set to true by check.sh's generated driver, which supplies its own top-level
// geometry. A case file renders its own view only when opened directly.
// (Top-level assignments are scope-wide and last-one-wins, so the driver's
// FILLET_DRIVER = true reaches the guard inside the included case.)
FILLET_DRIVER = false;

// Set from the command line (render.sh FAST=1) to drop the diff column, which is
// the only expensive part of the picture — it dilates both solids.
FILLET_NO_DIFF = false;

// ---- sectioning viewers ------------------------------------------------------

// Thin-slab section: intersect children with a thin horizontal slab at height z.
// XY size is deliberately huge so it covers the whole laid-out scene; only z and
// thickness matter for what the section shows.
module fillet_section(z = 0, thickness = 0.6, extent = 4000) {
  intersection() {
    children();
    translate([0, 0, z]) cube([extent, extent, thickness], center = true);
  }
}

// Vertical thin-slab section (normal +Y), for axis-symmetric ring features where
// a cut through the axis shows the rho-z profile and the ring tool on both sides.
module fillet_vsection(y = 0, thickness = 0.6, extent = 4000) {
  intersection() {
    children();
    translate([0, y, 0]) cube([extent, thickness, extent], center = true);
  }
}

// Stack several thin-slab sections at different heights, translated apart along
// +Y so the section EVOLVING toward a junction is visible at a glance (used for
// the 3-/4-face meeting-point cases).
module fillet_contour_stack(zs, thickness = 0.6, spacing = 90, extent = 4000) {
  for (i = [0 : len(zs) - 1])
    translate([0, i * spacing, 0])
      fillet_section(z = zs[i], thickness = thickness, extent = extent)
        children();
}

// A clip region large enough to select everything — for cases that compare the
// whole tool rather than one feature of it.
module fillet_clip_all(extent = 4000) cube(extent, center = true);

// ---- applying a tool ---------------------------------------------------------

// children(0) = model, children(1) = tool. Concave tools are unioned, convex
// tools subtracted, matching the operator's documented calling convention.
module fillet_apply(sign) {
  if (sign == "union") union() { children(0); children(1); }
  else if (sign == "subtract") difference() { children(0); children(1); }
  else assert(false, str("case sign must be \"union\" or \"subtract\", got: ", sign));
}

// ---- tolerance geometry ------------------------------------------------------

// L-infinity dilation by t (a cube "ball"): cheap, and enough to swallow the
// thin slivers between two tessellations of the same surface.
module fillet_dilate(t) minkowski() { children(); cube(2 * t, center = true); }

// Two-sided containment residual. children(0) = candidate, children(1) = reference.
//
// An exact symmetric difference is never empty — the operator tessellates arcs
// at $fa, the reference uses cylinder/rotate_extrude at $fn, so facets never
// line up. Instead dilate each side by t before subtracting:
//
//     (candidate - dilate(reference, t))  and  (reference - dilate(candidate, t))
//
// The union of the two is empty exactly when Hausdorff(candidate, reference) < t.
// Pick t above the arc chord error and below the smallest defect worth catching
// (cases use 2% of the radius). Tessellation slivers vanish; a wrong radius, a
// missing corner or a gouge survives.
module fillet_residual(t) {
  union() {
    difference() { children(0); fillet_dilate(t) children(1); }
    difference() { children(1); fillet_dilate(t) children(0); }
  }
}

// Reference-free sanity check.
// children(0) = model, children(1) = candidate tool, children(2) = clip region.
//
// A tool of size d may only disturb the model within distance d of it, whichever
// way the sign goes, so the applied result and the model must lie within each
// other's d-dilation. Empty means no gouge, no runaway bead, no wrong-direction
// tool. This is the check that covers the junction cases, which have no
// closed-form reference at all.
//
// Both sides are clipped first. Cutting them with the same region leaves the
// comparison meaningful — the artificial cut faces coincide and cancel — and it
// is what keeps the ring cases tractable: dilating a whole plate-and-boss
// through CGAL's Nef kernel does not finish, dilating the annular chunk the case
// is actually about takes seconds.
module fillet_sandwich(d, sign) {
  union() {
    difference() {
      intersection() { fillet_apply(sign) { children(0); children(1); } children(2); }
      fillet_dilate(d) intersection() { children(0); children(2); }
    }
    difference() {
      intersection() { children(0); children(2); }
      fillet_dilate(d) intersection() { fillet_apply(sign) { children(0); children(1); } children(2); }
    }
  }
}

// ---- the picture -------------------------------------------------------------

// One variant of a case, as six columns along X. children:
//   0 = model, 1 = reference tool, 2 = candidate tool, 3 = clip region
//
//   [ model ] [ ref applied ] [ cand applied ] [ ref tool ] [ cand tool ] [ diff ]
//
// The tool columns and the diff are clipped to the case's region of interest, so
// what you see in columns 4-6 is what the "tool" check actually compares; the
// first three columns are the whole model, unclipped.
//
// Each column slices itself rather than the row being sliced as a whole: an
// intersection outside color() produces an uncoloured result, so the slab has to
// happen inside the colour wrapper or every column comes out render-yellow.
// Within a column the slice still comes last, after any dilation.
module fillet_row(sign, t, slice, dx = 100) {
  color("Gainsboro") fillet_slice(slice) children(0);
  translate([1 * dx, 0, 0]) color("PaleGreen") fillet_slice(slice)
    fillet_apply(sign) { children(0); children(1); }
  translate([2 * dx, 0, 0]) color("LightSkyBlue") fillet_slice(slice)
    fillet_apply(sign) { children(0); children(2); }
  translate([3 * dx, 0, 0]) color("SeaGreen") fillet_slice(slice)
    intersection() { children(1); children(3); }
  translate([4 * dx, 0, 0]) color("SteelBlue") fillet_slice(slice)
    intersection() { children(2); children(3); }
  if (!FILLET_NO_DIFF)
    translate([5 * dx, 0, 0]) color("Red") fillet_slice(slice)
      fillet_residual(t) {
        intersection() { children(2); children(3); }
        intersection() { children(1); children(3); }
      }
}

// Cut the scene down to something one flat image can show, per the case's
// CASE_SLICE spec:
//   ["top",   z]              horizontal thin slab at height z
//   ["front", y]              vertical thin slab through y (ring features)
//   ["stack", zs, spacing]    several horizontal slabs laid out along +Y, so the
//                             section evolving toward a junction is visible at once
// Applied per column, always as the last step: intersecting with a slab commutes
// with union and difference, but NOT with the dilation the diff column uses.
module fillet_slice(spec) {
  if (spec[0] == "top") fillet_section(z = spec[1]) children();
  else if (spec[0] == "front") fillet_vsection(y = spec[1]) children();
  else if (spec[0] == "stack")
    fillet_contour_stack(spec[1], spacing = is_undef(spec[2]) ? 90 : spec[2]) children();
  else assert(false, str("unknown slice kind: ", spec[0]));
}

// The whole picture: one sliced row per variant, laid out along +Y, small size
// first. $case_size is bound to the variant's size for the row, so a case's tool
// modules take no arguments and read it as a special variable — the same
// dynamic scoping $fn uses, and what lets the picture and the headless checks
// drive one set of case modules.
// children are the case's four contract modules, as for fillet_row.
// Variants step along +Y by dy; ring cases that are cut vertically pass dy = 0
// and step along +Z instead, so every row stays in the cut plane.
//
// The four children are re-listed by index rather than forwarded with a bare
// children(): forwarding collapses them into a single group, and fillet_row
// would then see one child — the union of model, both tools and the clip
// region — instead of four it can lay out separately.
module fillet_case_view(sign, variants, slice, dy = 120, dx = 100, dz = 0) {
  for (i = [0 : len(variants) - 1])
    translate([0, i * dy, i * dz])
      let($case_size = variants[i][1])
        fillet_row(sign, variants[i][3], slice, dx)
          { children(0); children(1); children(2); children(3); }
}

// ---- the checks --------------------------------------------------------------

// Emits the geometry whose emptiness is one check's verdict. children are the
// same four as fillet_row. Polarity per kind (check.sh applies it):
//
//   tool      empty = PASS   candidate matches the hand reference within t
//   sandwich  empty = PASS   candidate disturbs the model only within size
//   emits     empty = FAIL   candidate produced a tool solid at all
//
module fillet_run_check(kind, t, sign, size) {
  if (kind == "tool")
    fillet_residual(t) {
      intersection() { children(2); children(3); }
      intersection() { children(1); children(3); }
    }
  else if (kind == "sandwich")
    fillet_sandwich(size, sign) { children(0); children(2); children(3); }
  else if (kind == "emits")
    intersection() { children(2); children(3); }
  else
    assert(false, str("unknown check kind: ", kind));
}
