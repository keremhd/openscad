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

#include <algorithm>
#include <cmath>
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

// Does this solid enclose any material? Not the same question as IsEmpty():
// wherever a boolean's operands touch on a set of zero measure, Manifold leaves
// a volumeless four-triangle shell behind, and an exact containment is precisely
// such a case — so a residual that is geometrically nothing still has triangles
// in it. The cutoff is nine orders below the solids being compared: far under
// any defect a tolerance-based check could be about, far above float noise.
bool enclosesNothing(const Manifold& m, double scale)
{
  if (m.IsEmpty()) return true;
  const double keepAbove = 1e-9 * scale;
  for (const auto& part : m.Decompose())
    if (std::abs(part.Volume()) > keepAbove) return false;
  return true;
}

// Two solids agree within t. Both being empty counts as agreement; one empty and
// one not does not, which is what makes this catch an operator that silently
// builds nothing.
bool agreesWithin(const Manifold& a, const Manifold& b, double t)
{
  const double scale = std::max(std::abs(a.Volume()), std::abs(b.Volume()));
  return enclosesNothing(residual(a, b, t), scale);
}

// How finely the hand-written references tessellate their arcs. Manifold's own
// default is 12 segments a circle, whose chord error is larger than the 2 %
// tolerance these cases compare at — a reference coarser than the tolerance
// fails a correct tool, so it is set here rather than inherited.
constexpr int kRefSegments = 64;

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

// The fillet bead along one axis-aligned edge running in z: the corner square
// minus a quarter cylinder, the idiom a user would write by hand. `concave`
// picks which side of the corner the material is on, and with it whether the
// arc is subtracted from the reentrant square or from the sharp sliver.
Manifold refBeadZ(double cx, double cy, double r, double h, double z0, bool concave)
{
  const double s = concave ? 1.0 : -1.0;
  const double bx = concave ? cx : cx - r;
  const double by = concave ? cy : cy - r;
  const Manifold square = Manifold::Cube(vec3(r, r, h), false).Translate(vec3(bx, by, z0));
  const Manifold arc = Manifold::Cylinder(h + 2, r, r, kRefSegments, false)
                         .Translate(vec3(cx + s * r, cy + s * r, z0 - 1));
  return square - arc;
}

// A torus of tube radius r whose tube center circle sits at radius rho in the
// z = zc plane — the surface the rolling ball sweeps around a closed ring edge.
Manifold refTorus(double rho, double zc, double r)
{
  manifold::SimplePolygon circle;
  for (int i = 0; i < kRefSegments; ++i) {
    const double a = 2.0 * M_PI * i / kRefSegments;
    circle.emplace_back(rho + r * std::cos(a), r * std::sin(a));
  }
  return Manifold::Revolve({circle}, kRefSegments).Translate(vec3(0, 0, zc));
}

// The two closed-ring references, both the annulus-prism-minus-torus idiom of
// the hole-mouth sanity case: an annulus one radius tall spanning the ring, with
// the swept ball taken out of it.
Manifold refRoundHoleMouth(double rh, double zf, double r)
{
  const Manifold annulus =
    Manifold::Cylinder(r, rh + r, rh + r, kRefSegments, false) -
    Manifold::Cylinder(r + 2, rh, rh, kRefSegments, false).Translate(vec3(0, 0, -1));
  return (annulus - refTorus(rh + r, 0.0, r)).Translate(vec3(0, 0, zf - r));
}

Manifold refFilletBossBase(double rb, double zf, double r)
{
  const Manifold annulus =
    Manifold::Cylinder(r, rb + r, rb + r, kRefSegments, false) -
    Manifold::Cylinder(r + 2, rb, rb, kRefSegments, false).Translate(vec3(0, 0, -1));
  return (annulus - refTorus(rb + r, r, r)).Translate(vec3(0, 0, zf));
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

Manifold roundToolFor(const Manifold& target, double r, bool concave, int arcSegments,
                      double thresholdDeg = 45.0)
{
  const MergedMesh mm = mergeMesh(target.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, thresholdDeg, concave));
  return buildRoundSolid(mm, adj, chains, r, concave, arcSegments);
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

TEST_CASE("fillet_tool: the inner corner bead matches the hand-written reference")
{
  // Same L as the chamfer case, and the same single crease, but now the tool has
  // to be the corner square with a quarter cylinder taken out of it rather than
  // the square's diagonal.
  const double r = 3.0, h = 10.0;
  const auto model = box(20.0, 6.0, h) + box(6.0, 20.0, h);

  const auto tool = roundToolFor(model, r, /*concave=*/true, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  const auto ref = refBeadZ(6.0, 6.0, r, h, 0.0, /*concave=*/true);
  CHECK(agreesWithin(tool, ref, 0.02 * r));
  CHECK(agreesWithin(model + tool, model + ref, 0.02 * r));

  // The bead is strictly smaller than the chamfer that shares its tangency
  // points, by exactly the circular segment. Without this a tool that quietly
  // skipped the subtraction would still match a chamfer-shaped reference.
  CHECK_FALSE(agreesWithin(tool, toolFor(model, r, /*concave=*/true), 0.02 * r));
}

TEST_CASE("round_tool: one cube edge matches the hand-written reference")
{
  // Clipped to the one edge the reference covers, clear of the corners, for the
  // same reason the bevel case is: the tool takes all twelve.
  const double s = 16.0, r = 2.0;
  const auto model = box(s, s, s);

  const auto tool = roundToolFor(model, r, /*concave=*/false, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  const double margin = 2.0 * r;
  const auto clip = Manifold::Cube(vec3(2 * margin, 2 * margin, s - 2 * margin), false)
                      .Translate(vec3(s - margin, s - margin, margin));

  const auto ref = refBeadZ(s, s, r, s, 0.0, /*concave=*/false);
  CHECK(agreesWithin(tool ^ clip, ref ^ clip, 0.02 * r));
  CHECK(agreesWithin((model - tool) ^ clip, (model - ref) ^ clip, 0.02 * r));
}

TEST_CASE("round_tool: the hole mouth matches annulus prism minus torus")
{
  // The sanity case the whole operator was specified against: rounding the mouth
  // of a through hole has to give the shape a user would write by hand as an
  // annulus prism with a torus taken out of it. A closed convex ring, so it also
  // covers the chain wrapping.
  const double w = 60.0, t = 20.0, rh = 10.0, r = 3.0;
  const auto model = box(w, w, t).Translate(vec3(-w / 2, -w / 2, 0)) -
                     Manifold::Cylinder(t + 2, rh, rh, 64, false).Translate(vec3(0, 0, -1));

  const auto tool = roundToolFor(model, r, /*concave=*/false, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  // The operator rounds the far rim and the plate's own outer edges too; the
  // reference covers the top mouth, so the comparison is clipped to it.
  const double c = 2.0 * r;
  const auto clip = Manifold::Cylinder(c + 1, rh + c, rh + c, 64, false).Translate(vec3(0, 0, t - c));

  const auto ref = refRoundHoleMouth(rh, t, r);
  CHECK(agreesWithin(tool ^ clip, ref ^ clip, 0.02 * r));
  CHECK(agreesWithin((model - tool) ^ clip, (model - ref) ^ clip, 0.02 * r));
}

TEST_CASE("fillet_tool: a boss base fillet larger than the boss still matches")
{
  // The concave closed ring, at a radius twice the boss's own: the section plane
  // has to be the meridian one and the ring has to stay a ring, or the bead
  // wanders off the axis. The section plane is taken from the two wall normals
  // rather than from the spine direction precisely so this holds.
  const double rb = 12.0, r = 24.0;
  const auto model = box(80.0, 80.0, 15.0).Translate(vec3(-40, -40, -15)) +
                     Manifold::Cylinder(40.0, rb, rb, 48, false);

  const auto tool = roundToolFor(model, r, /*concave=*/true, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  const auto ref = refFilletBossBase(rb, 0.0, r);
  CHECK(agreesWithin(tool, ref, 0.02 * r));
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
