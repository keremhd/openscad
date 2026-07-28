// Shape comparison for the fillet tools: does the solid the operator builds
// agree with a hand-written reference, everywhere, to within a tolerance?
//
// An exact symmetric difference is never empty — the operator's arcs and a
// hand-written cylinder never tessellate the same way — so each side is dilated
// by t before subtracting. The union of the two residuals is empty exactly when
// the Hausdorff distance between the solids is below t. Pick t above the chord
// error of the coarser tessellation and below the smallest defect worth
// catching, and tessellation slivers vanish while a wrong size, a missing corner
// or a gouge survives.
//
// The same comparison exists as a shell harness driven through the OpenSCAD
// language, where the dilation is a minkowski() and therefore runs on CGAL's Nef
// kernel — seconds per check. Here it is a few hundred convex hulls fed to
// Manifold, which is fast enough to sit in the unit-test run, and the failure
// arrives as a named case rather than a picture to interpret.

#include <catch2/catch_all.hpp>

#ifdef ENABLE_MANIFOLD

#include "geometry/fillet/FilletBuilder_internal.h"

#include <cstddef>
#include <vector>

#include <manifold/manifold.h>

using namespace fillet::detail;
using manifold::Manifold;
using manifold::vec3;

namespace {

// Minkowski sum with a cube of half-extent t, i.e. dilation by t in L-infinity.
//
// The sum of a solid with a convex body is the solid itself unioned with the
// sum of each of its boundary triangles, and a triangle plus a cube is the hull
// of the 24 points you get by offsetting each of its corners to each corner of
// the cube. That makes this exact rather than an approximation, at the price of
// one hull per triangle — affordable on the small solids these cases use.
Manifold dilate(const Manifold& m, double t)
{
  if (m.IsEmpty()) return m;

  const manifold::MeshGL64 mesh = m.GetMeshGL64();
  const size_t stride = mesh.numProp;

  std::vector<Manifold> pieces{m};
  for (size_t i = 0; i + 2 < mesh.triVerts.size(); i += 3) {
    std::vector<vec3> pts;
    pts.reserve(24);
    for (size_t k = 0; k < 3; ++k) {
      const size_t base = mesh.triVerts[i + k] * stride;
      const double x = mesh.vertProperties[base];
      const double y = mesh.vertProperties[base + 1];
      const double z = mesh.vertProperties[base + 2];
      for (int corner = 0; corner < 8; ++corner)
        pts.emplace_back(x + ((corner & 1) ? t : -t), y + ((corner & 2) ? t : -t),
                         z + ((corner & 4) ? t : -t));
    }
    Manifold piece = Manifold::Hull(pts);
    if (!piece.IsEmpty()) pieces.push_back(std::move(piece));
  }
  return Manifold::BatchBoolean(pieces, manifold::OpType::Add);
}

// The two-sided containment residual: what of either solid lies further than t
// from the other. Empty means the two agree to within t everywhere.
Manifold residual(const Manifold& a, const Manifold& b, double t)
{
  return (a - dilate(b, t)) + (b - dilate(a, t));
}

// Two solids agree within t. Both being empty counts as agreement; one empty and
// one not does not, which is what makes this catch an operator that silently
// builds nothing.
bool agreesWithin(const Manifold& a, const Manifold& b, double t)
{
  return residual(a, b, t).IsEmpty();
}

// The chamfer/bevel wedge along one axis-aligned edge running in z: a triangular
// prism, and convex, so the hull of its six corners is the whole of it.
Manifold refWedgeZ(double cx, double cy, double t, double h, double z0, bool concave)
{
  const double s = concave ? 1.0 : -1.0;
  std::vector<vec3> pts;
  for (double z : {z0, z0 + h}) {
    pts.emplace_back(cx, cy, z);
    pts.emplace_back(cx + s * t, cy, z);
    pts.emplace_back(cx, cy + s * t, z);
  }
  return Manifold::Hull(pts);
}

// The tool solid the operator builds for a target, without going through the
// node or the geometry evaluator.
Manifold toolFor(const Manifold& target, double t, bool concave, double thresholdDeg = 45.0)
{
  const MergedMesh mm = mergeMesh(target.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, thresholdDeg, concave));
  return buildWedgeSolid(mm, adj, chains, t, concave);
}

Manifold box(double sx, double sy, double sz) { return Manifold::Cube(vec3(sx, sy, sz), false); }

}  // namespace

TEST_CASE("comparison: tessellation differences pass, size differences do not")
{
  // Calibration of the instrument itself, before any of it is pointed at the
  // operator. Two tessellations of one cylinder differ by the chord error of the
  // coarser one, r(1 - cos(pi/n)) = 0.048 at r=10, n=32: inside a tolerance of
  // 0.1, outside one of 0.01. A cylinder 10% wider is outside both.
  const double r = 10.0, h = 5.0;
  const auto coarse = Manifold::Cylinder(h, r, r, 32, false);
  const auto fine = Manifold::Cylinder(h, r, r, 64, false);
  const auto wide = Manifold::Cylinder(h, 1.1 * r, 1.1 * r, 64, false);

  CHECK(agreesWithin(coarse, fine, 0.1));
  CHECK_FALSE(agreesWithin(coarse, fine, 0.01));
  CHECK_FALSE(agreesWithin(coarse, wide, 0.1));

  // An empty candidate is not a match for a solid reference — the case that
  // separates "the operator agrees" from "the operator did nothing".
  CHECK_FALSE(agreesWithin(Manifold(), fine, 0.1));
}

TEST_CASE("chamfer_tool: the inner corner wedge matches the hand-written reference")
{
  // The L of two overlapping bars has exactly one concave crease, running the
  // full height at (x,y) = (6,6), so the whole tool is one wedge and no clipping
  // is needed to compare against one.
  const double t = 3.0, h = 10.0;
  const auto model = box(20.0, 6.0, h) + box(6.0, 20.0, h);

  const auto tool = toolFor(model, t, /*concave=*/true);
  REQUIRE_FALSE(tool.IsEmpty());

  const auto ref = refWedgeZ(6.0, 6.0, t, h, 0.0, /*concave=*/true);
  CHECK(agreesWithin(tool, ref, 0.02 * t));

  // And the point of the tool: unioning it fills the crease, matching the model
  // with the reference wedge already in it.
  CHECK(agreesWithin(model + tool, model + ref, 0.02 * t));
}

TEST_CASE("bevel_tool: one cube edge matches the hand-written reference")
{
  // Every solid has plenty of convex edges and the tool takes all of them, so
  // the comparison is clipped to a box around the one edge the reference covers
  // — clear of the corners, where the two constructions genuinely differ until
  // junction cells exist.
  const double s = 16.0, t = 2.0;
  const auto model = box(s, s, s);

  const auto tool = toolFor(model, t, /*concave=*/false);
  REQUIRE_FALSE(tool.IsEmpty());

  const double margin = 2.0 * t;
  const auto clip =
    Manifold::Cube(vec3(2 * margin, 2 * margin, s - 2 * margin), false)
      .Translate(vec3(s - margin, s - margin, margin));

  const auto ref = refWedgeZ(s, s, t, s, 0.0, /*concave=*/false);
  CHECK(agreesWithin(tool ^ clip, ref ^ clip, 0.02 * t));

  // Applied, i.e. subtracted, which is the sign the caller uses.
  CHECK(agreesWithin((model - tool) ^ clip, (model - ref) ^ clip, 0.02 * t));
}

TEST_CASE("comparison: a wedge of the wrong size is caught")
{
  // The self-test the shell harness carries too: the comparison has to fail when
  // it should, or a green run means nothing. A chamfer 20% too deep is well
  // outside the 2% tolerance the cases above pass at.
  const double t = 3.0, h = 10.0;
  const auto model = box(20.0, 6.0, h) + box(6.0, 20.0, h);

  const auto tool = toolFor(model, t, /*concave=*/true);
  const auto refTooBig = refWedgeZ(6.0, 6.0, 1.2 * t, h, 0.0, /*concave=*/true);
  CHECK_FALSE(agreesWithin(tool, refTooBig, 0.02 * t));
}

#endif  // ENABLE_MANIFOLD
